#pragma once
#include <QAudioBuffer>
#include <QVariantList>
#include <array>
#include <algorithm>
#include <cmath>

// Five overlapping band-pass filters measure decoded PCM, before volume control.
// Channels are measured separately so out-of-phase stereo cannot cancel out.
class AudioLevels {
  struct Filter { double b=0,a1=0,a2=0; std::array<double,8> z1{},z2{}; };
  std::array<Filter,5> filters{};
  std::array<double,5> energy{};
  int rate=0,channels=0;
  qint64 samples=0;
public:
  void reset() {filters={};energy={};rate=channels=0;samples=0;}
  // The same filters serve decoded buffers as they play and whole files
  // analysed ahead of playback, so both produce the same numbers.
  void configure(int sampleRate,int channelCount) {
    channelCount=std::min(8,channelCount);
    if(rate==sampleRate && channels==channelCount)return;
    reset();rate=sampleRate;channels=channelCount;
    constexpr std::array<double,5> centers{100,350,1200,3500,10000};
    for(int band=0;band<5;++band){
      if(centers[band]>=rate*0.45)continue;
      const double w=2*3.14159265358979323846*centers[band]/rate;
      const double alpha=std::sin(w)/(2*0.65),denominator=1+alpha;
      filters[band].b=alpha/denominator;
      filters[band].a1=-2*std::cos(w)/denominator;
      filters[band].a2=(1-alpha)/denominator;
    }
  }
  void sample(int channel,double x) {
    x=std::isfinite(x)?std::clamp(x,-1.0,1.0):0;
    for(int band=0;band<5;++band){
      auto &f=filters[band];const double y=f.b*x+f.z1[channel];
      f.z1[channel]=-f.a1*y+f.z2[channel];f.z2[channel]=-f.b*x-f.a2*y;
      energy[band]+=y*y;
    }
  }
  // Flush inaudible filter tails before they become costly denormal values.
  void settle(qint64 counted) {
    for(auto &f:filters)for(int c=0;c<channels;++c){if(std::abs(f.z1[c])<1e-15)f.z1[c]=0;if(std::abs(f.z2[c])<1e-15)f.z2[c]=0;}
    samples+=counted;
  }
  void process(const QAudioBuffer &buffer) {
    const auto format=buffer.format();
    if(!buffer.isValid() || !format.isValid())return;
    configure(format.sampleRate(),format.channelCount());
    const auto bytes=buffer.constData<char>();
    const int frameCount=buffer.frameCount(),bytesPerFrame=format.bytesPerFrame(),bytesPerSample=format.bytesPerSample();
    for(qsizetype frame=0;frame<frameCount;++frame)
      for(int channel=0;channel<channels;++channel)
        sample(channel,format.normalizedSampleValue(bytes+frame*bytesPerFrame+channel*bytesPerSample));
    settle(qint64(frameCount)*channels);
  }
  // Interleaved float samples, as ffmpeg writes them.
  void process(const float *interleaved,qsizetype frames,int channelCount,int sampleRate) {
    configure(sampleRate,channelCount);
    const int stride=channelCount;
    for(qsizetype frame=0;frame<frames;++frame)
      for(int channel=0;channel<channels;++channel)sample(channel,interleaved[frame*stride+channel]);
    settle(qint64(frames)*channels);
  }
  QVariantList takeLevels() {
    QVariantList result;result.reserve(5);
    for(double sum:energy){
      const double rms=samples>0?std::sqrt(sum/samples):0;
      const double db=20*std::log10(std::max(rms,0.000001));
      result.append(std::round(std::clamp((db+54)/48,0.0,1.0)*100)/100);
    }
    energy={};samples=0;return result;
  }
};

// Mean square of decoded PCM for playback levelling. This is a broadband RMS
// estimate, not an EBU R128 meter: it costs one multiply per sample and only
// has to be stable enough to compare one recording against another.
class LoudnessMeter {
  double sum=0;
  qint64 samples=0;
  int rate=0,used=0;
public:
  void reset() {sum=0;samples=0;rate=0;used=0;}
  void process(const QAudioBuffer &buffer) {
    const auto format=buffer.format();
    if(!buffer.isValid()||!format.isValid())return;
    rate=format.sampleRate();
    const int channels=std::min(8,format.channelCount());
    used=channels;
    const auto bytes=buffer.constData<char>();
    const int frames=buffer.frameCount(),bytesPerFrame=format.bytesPerFrame(),bytesPerSample=format.bytesPerSample();
    for(qsizetype frame=0;frame<frames;++frame)
      for(int channel=0;channel<channels;++channel){
        double x=format.normalizedSampleValue(bytes+frame*bytesPerFrame+channel*bytesPerSample);
        if(!std::isfinite(x))continue;
        x=std::clamp(x,-1.0,1.0);sum+=x*x;++samples;
      }
  }
  // Short measurements describe an intro, not a recording. Wait for enough audio.
  double seconds() const {return rate>0&&used>0?double(samples)/(double(rate)*used):0;}
  bool ready() const {return seconds()>=45;}
  double levelDb() const {
    if(samples<=0)return 0;
    return 20*std::log10(std::max(std::sqrt(sum/samples),1e-6));
  }
};
