#!/usr/bin/env python3
"""Repeatable verification. All profiles, logs, and captures stay in the report folder."""
import argparse, datetime, json, os, pathlib, re, shutil, subprocess, sys, time
from xml.sax.saxutils import escape
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--offline',action='store_true',help='Skip network-dependent playback checks; report them as skipped')
p.add_argument('--output',type=pathlib.Path,help='Report folder (created with private permissions)')
p.add_argument('--jobs',type=int,default=4)
p.add_argument('--build-dir',type=pathlib.Path,help='Reuse a diagnostic build directory')
a=p.parse_args()
out=(a.output or root/'verification'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
out.mkdir(parents=True,exist_ok=False);out.chmod(0o700)
rows=[]
macos=sys.platform=='darwin'
# Where the real library lives. No stage may run with a profile that is, lies
# inside or holds any of these: on macOS the app ignores XDG_* entirely, and a
# run of this script once wrote every suite's fixtures into the real library.
home=pathlib.Path.home()
real=[home/'Library/Application Support/Sung',home/'Library/Caches/Sung',home/'Library/Preferences'] if macos else \
     [pathlib.Path(os.environ.get('XDG_DATA_HOME',home/'.local/share'))/'Sung',pathlib.Path(os.environ.get('XDG_CONFIG_HOME',home/'.config'))/'Sung',pathlib.Path(os.environ.get('XDG_CACHE_HOME',home/'.cache'))/'Sung']
def resolved(path):
    path=pathlib.Path(path).absolute();rest=[]
    while not path.exists() and path!=path.parent:rest.insert(0,path.name);path=path.parent
    return path.resolve().joinpath(*rest)
def unsafe(env):
    if not env or not env.get('MUSIX_PROFILE'):return 'no MUSIX_PROFILE'
    mine=resolved(env['MUSIX_PROFILE'])
    for place in map(resolved,real):
        if mine==place or place in mine.parents or mine in place.parents:return f'MUSIX_PROFILE {mine} overlaps {place}'
    return None
def stage(name,cmd,timeout=600,env=None,app_free=False,retry=False):
    print(f'▶ {name}',flush=True)
    # Anything that may start the app has to carry a safe profile; only the
    # stages that provably never start it (configure, build) may go without.
    if not app_free and (why:=unsafe(env)):
        sys.exit(f'verify: refusing to run {name}: {why}')
    start=time.monotonic()
    target=out/(name+'.log')
    try:
        with target.open('w') as log:
            r=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
        text=target.read_text(errors='replace')
        problems=re.findall(r'(?:ReferenceError|TypeError|Binding loop|Unable to assign|Cannot assign|FAIL)[^\n]*',text)
        good=r.returncode==0 and not problems
        why='; '.join(problems[:3]) if problems else f'exit {r.returncode}'
    except (subprocess.TimeoutExpired,OSError) as e:
        good=False;why=str(e)
    if not good and retry:
        # A live stage streams real songs, and YouTube sometimes fails one
        # that plays on the next try. One more run; both results are kept.
        print(f'  FAIL {name}, trying once more: {why}',flush=True)
        target.rename(out/(name+'.first-try.log'))
        # A stage that insists on a fresh output folder gets one again.
        for arg in cmd:
            if isinstance(arg,str) and arg.startswith(str(out)+'/') and pathlib.Path(arg).is_dir() and pathlib.Path(arg).name==name:
                pathlib.Path(arg).rename(pathlib.Path(arg).with_name(name+'.first-try'))
        again=stage(name,cmd,timeout,env,app_free,retry=False)
        rows[-1]['detail']=f'first try failed ({why}); '+('passed' if again else 'failed')+' on the second'
        return again
    rows.append(dict(stage=name,status='pass' if good else 'fail',seconds=round(time.monotonic()-start,2),detail=why,log=target.name))
    print(f'  {"PASS" if good else "FAIL"} {name} ({rows[-1]["seconds"]}s)',flush=True)
    return good
subprocess.run([sys.executable,str(root/'scripts/backup-profile.py'),'verify-py'],check=True)
build=a.build_dir.resolve() if a.build_dir else out/'build'
# macOS builds an application bundle; every other platform a bare executable.
app=build/'musix.app/Contents/MacOS/musix' if (build/'musix.app').is_dir() else build/'musix'
env=os.environ.copy()
# Do not inherit a desktop-forced threaded render loop into the software harness.
# Qt Quick Shapes can race window teardown there; native GPU tests keep their own loop.
env.update(QT_FORCE_STDERR_LOGGING='1',QT_QPA_PLATFORMTHEME='generic',QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QSG_RENDER_LOOP='basic',QT_FFMPEG_DECODING_HW_DEVICE_TYPES=',',QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=',')
# Isolated XDG_DATA_HOME also hides user-installed fonts from fontconfig.
# Expose only the requested font directory, not the user's application data.
if shutil.which('fc-match'):
    font_file=subprocess.check_output(['fc-match','Google Sans Flex','-f','%{file}'],text=True).strip()
    font_config=out/'fonts.conf'
    font_config.write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>'+escape(str(pathlib.Path(font_file).parent))+'</dir></fontconfig>')
    env['FONTCONFIG_FILE']=str(font_config)

def profile(name):
    e=env.copy();base=out/name
    for var,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache')]:e[var]=str(base/folder)
    e['MUSIX_PROFILE']=str(base)
    return e
# The fixture catalogue imports the real helper for local files, which needs
# the project's own Python; the one macOS ships is too old for it.
fixture_python=str(root/'runtime/bin/python') if macos and (root/'runtime/bin/python').exists() else '/usr/bin/python3'
ready=stage('configure',app_free=True,cmd=['cmake','-S',str(root),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON','-DSUNG_DIAGNOSTICS=ON'])
if ready: ready=stage('build',app_free=True,cmd=['cmake','--build',str(build),'--parallel',str(max(1,a.jobs))])
if ready:
    stage('backend',['ctest','--test-dir',str(build),'--output-on-failure'],600,profile('unit-profile'))
    stage('catalog',['python3','-m','unittest','discover','-s',str(root/'tests'),'-p','test_*.py'],60,profile('catalog-profile'))
    if macos: rows.append(dict(stage='mpris',status='skipped',detail='MPRIS is D-Bus, which macOS does not have'))
    elif shutil.which('dbus-run-session') and shutil.which('qdbus6'):
        stage('mpris',['dbus-run-session','--','python3',str(root/'tests/mpris_test.py'),str(app)],40,profile('mpris-profile'))
    else: rows.append(dict(stage='mpris',status='fail',detail='dbus-run-session and qdbus6 are required'))
    stage("immersive-regression",[sys.executable,str(root/"tests/immersive_regression.py"),"--binary",str(app),"--output",str(out/"immersive-regression")],780,profile("immersive-regression-profile"))
    e=profile("interaction-refinement-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"interaction-refinement"))
    stage("interaction-refinement",[str(app),"--isolated","--interaction-refinement-test"],120,e)
    e=profile("listening-refinement-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"listening-refinement"))
    stage("listening-refinement",[str(app),"--isolated","--listening-refinement-test"],90,e)
    e=profile("visual-refinement-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"visual-refinement"))
    stage("visual-refinement",[str(app),"--isolated","--visual-refinement-test"],90,e)
    e=profile("online-artwork-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"online-artwork"))
    stage("online-artwork",[str(app),"--isolated","--online-artwork-test"],60,e)
    e=profile("local-artwork-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"local-artwork"))
    stage("local-artwork",[str(app),"--isolated","--local-artwork-test"],90,e)
    e=profile("folder-import-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"folder-import"))
    stage("folder-import",[str(app),"--isolated","--folder-import-test"],60,e)
    e=profile('search-selection-profile');e.update(SUNG_HELPER=str(root/'tests/catalog_fixture.py'),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/'search-selection'))
    stage('search-selection',[str(app),'--isolated','--search-selection-test'],120,e)
    e=profile("qol-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"qol"))
    stage("qol",[str(app),"--isolated","--qol-test"],60,e)
    e=profile("interaction-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"interaction"))
    stage("interaction",[str(app),"--isolated","--interaction-test"],90,e)
    e=profile("product-polish-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_BUFFER_FIXTURE="1",SUNG_TEST_OUTPUT=str(out/"product-polish"))
    stage("product-polish",[str(app),"--isolated","--product-polish-test"],90,e)
    e=profile("library-polish-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"library-polish"))
    stage("library-polish",[str(app),"--isolated","--library-polish-test"],90,e)
    e=profile("playback-polish-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"playback-polish"))
    stage("playback-polish",[str(app),"--isolated","--playback-polish-test"],90,e)
    e=profile("audio-indicator-profile");e.update(SUNG_TEST_OUTPUT=str(out/"audio-indicator"))
    stage("audio-indicator",[str(app),"--isolated","--audio-indicator-test"],40,e)
    e=profile("visual-delight-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"visual-delight"))
    stage("visual-delight",[str(app),"--isolated","--visual-delight-test"],90,e)
    e=profile("library-qol-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"library-qol"))
    stage("library-qol",[str(app),"--isolated","--library-qol-test"],60,e)
    e=profile("visual-polish-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"visual-polish"))
    stage("visual-polish",[str(app),"--isolated","--visual-polish-test"],40,e)
    e=profile("ambient-immersive-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"ambient-immersive"))
    stage("ambient-immersive",[str(app),"--isolated","--ambient-immersive-test"],180,e)
    e=profile("personalization-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"personalization"))
    stage("personalization",[str(app),"--isolated","--personalization-test"],180,e)
    e=profile("home-rail-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"home-rail"))
    stage("home-rail",[str(app),"--isolated","--home-rail-test"],180,e)
    e=profile("onboarding-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"onboarding"))
    stage("onboarding",[str(app),"--isolated","--onboarding-test"],180,e)
    e=profile("library-exchange-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"library-exchange"))
    stage("library-exchange",[str(app),"--isolated","--library-exchange-test"],300,e)
    e=profile("backdrop-pulse-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"backdrop-pulse"))
    stage("backdrop-pulse",[str(app),"--isolated","--backdrop-pulse-test"],240,e)
    e=profile("playback-memory-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"playback-memory"))
    stage("playback-memory",[str(app),"--isolated","--playback-memory-test"],420,e)
    for name,flag in [("dynamic-color","--dynamic-color-test"),("navigation-motion","--navigation-motion-test"),
                      ("artist-hero","--artist-hero-test"),("singalong","--singalong-test"),
                      ("crossfade-ui","--crossfade-ui-test"),("track-details","--track-details-test"),
                      ("queue-history","--queue-history-test"),
                      ("window-wash","--window-wash-test"),
                      ("material-foundations","--material-foundations-test"),
                      ("material-components","--material-components-test"),
                      ("material-detail","--material-detail-test"),
                      ("material-expressive","--material-expressive-test"),
                      ("material-sizing","--material-sizing-test"),
                      ("material-scheme","--material-scheme-test"),
                      ("material-grain","--material-grain-test"),
                      ("material-scale","--material-scale-test"),
                      ("material-controls","--material-controls-test"),
                      ("material-anatomy","--material-anatomy-test"),
                      ("material-emphasis","--material-emphasis-test")]:
        e=profile(name+"-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/name))
        stage(name,[str(app),"--isolated",flag],420,e)
    e=profile("interface-audit-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON=fixture_python,SUNG_TEST_OUTPUT=str(out/"interface-audit"))
    stage("interface-audit",[str(app),"--isolated","--interface-audit-test"],300,e)
    if a.offline:
        rows.append(dict(stage='live-ui-and-audit',status='skipped',detail='--offline selected; streaming and live catalog not verified'))
    else:
        py=os.environ.get('SUNG_PYTHON',str(root/'runtime/bin/python'))
        if not pathlib.Path(py).exists():
            rows.append(dict(stage='live-runtime',status='fail',detail='Run scripts/setup.sh or set SUNG_PYTHON'))
        else:
            e=profile('online-artwork-live-profile');e.update(SUNG_PYTHON=py)
            stage('online-artwork-live',['python3',str(root/'tests/online_artwork_live.py'),'--output',str(out/'online-artwork-live'),'--binary',str(app)],1500,e,retry=True)
            # The packaged ffmpeg the live playback check fetches songs with; the
            # gate builds it first (scripts/release-checks.sh).
            packaged=os.environ.get('SUNG_PACKAGED_FFMPEG_DIR',str(root/'build-packaging/ffmpeg-out/bin'))
            for name,flag in [('native-features','--features-test'),('live-lyrics-motion','--lyrics-test'),('playback-recovery','--recovery-test'),('ui-playback','--ui-test'),('ui-audit','--audit')]:
                e=profile(name+'-profile');e.update(SUNG_PYTHON=py,SUNG_HELPER=str(root/'helper/catalog.py'),SUNG_TEST_OUTPUT=str(out/name),SUNG_PACKAGED_FFMPEG_DIR=packaged)
                stage(name,[str(app),'--isolated',flag],480,e,retry=True)
            e=profile('hidpi-profile');e.update(SUNG_PYTHON=py,SUNG_HELPER=str(root/'helper/catalog.py'),SUNG_TEST_OUTPUT=str(out/'hidpi'),QT_SCALE_FACTOR='1.6')
            stage('ui-hidpi',[str(app),'--isolated','--ui-test'],480,e,retry=True)
    if macos:
        rows.append(dict(stage='idle-performance',status='skipped',detail='tests/profile.py reads /proc, which macOS does not have'))
        rows.append(dict(stage='mini-performance',status='skipped',detail='tests/profile.py reads /proc, which macOS does not have'))
    else:
        stage('idle-performance',['python3',str(root/'tests/profile.py'),str(app)],20,profile('performance-profile'))
        stage('mini-performance',['python3',str(root/'tests/profile.py'),str(app),'--mini'],20,profile('mini-performance-profile'))
    stage('noctalia-preview',[str(app),'--isolated','--offline','--screenshot',str(out/'noctalia.png')],15,{**profile('theme-profile'),'SUNG_NOCTALIA_COLORS':os.environ.get('SUNG_NOCTALIA_COLORS',str(pathlib.Path.home()/'.local/share/color-schemes/noctalia.colors'))})
summary={'created':datetime.datetime.now().astimezone().isoformat(),'offline':a.offline,'passed':all(r['status']!='fail' for r in rows),'stages':rows,'diagnostic_binary_bytes':app.stat().st_size if ready else None,'note':'Screenshots require human visual review. Live checks depend on YouTube and the network. Diagnostic binary includes QtTest; measure the Release executable separately.'}
(out/'report.json').write_text(json.dumps(summary,indent=2)+'\n')
(out/'report.txt').write_text('\n'.join(f"{r['status'].upper():7} {r['stage']}: {r.get('detail','')}" for r in rows)+'\n')
print(f'Report: {out}/report.json',flush=True)
sys.exit(0 if summary['passed'] else 1)
