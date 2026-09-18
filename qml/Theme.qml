pragma Singleton
import QtQuick
QtObject {
    property color artworkSeed: "transparent"
    Behavior on artworkSeed {enabled:app.motion && app.artworkAccent;ColorAnimation {duration:240;easing.type:Easing.InOutCubic}}
    readonly property bool useArtwork: app.artworkAccent && artworkSeed.a > 0
    // A hand-picked Material source color, used when no cover is driving the theme.
    property color accentSeed: app.accentColor ? app.accentColor : "transparent"
    Behavior on accentSeed {enabled:app.motion;ColorAnimation {duration:240;easing.type:Easing.InOutCubic}}
    readonly property bool useAccent: !useArtwork && accentSeed.a > 0
    readonly property bool useSource: useArtwork || useAccent
    readonly property color sourceColor: useArtwork ? artworkSeed : accentSeed
    // Material spreads five tonal palettes around one source color and reads
    // every role off them at fixed tones. Surfaces included: that trace of the
    // cover's hue in the neutrals is what ties the window to the music.
    // The scheme depends on the variant and the contrast level as much as on
    // the source colour, so the binding has to read them or a change to either
    // would never reach the window.
    readonly property var roles: useSource ? (app.colorVariant, app.colorContrast, app.colorScheme(sourceColor,dark)) : ({})
    function role(name,fallback) {const c=roles[name];return c===undefined?fallback:c;}
    function blend(a,b,t) {return Qt.rgba(a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,1);}
    function luminance(c) {
        function linear(v) {return v<=0.04045?v/12.92:Math.pow((v+0.055)/1.055,2.4);}
        return 0.2126*linear(c.r)+0.7152*linear(c.g)+0.0722*linear(c.b);
    }
    function contrast(a,b) {let x=luminance(a),y=luminance(b);return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05);}
    function readable(seed,surfaces) {
        const end=dark?Qt.rgba(1,1,1,1):Qt.rgba(0,0,0,1);
        for(let i=0;i<=100;++i){const color=blend(seed,end,i/100);if(surfaces.every(s=>contrast(color,s)>=4.5))return color;}
        return end;
    }
    // --- Shape ---------------------------------------------------------------
    // Material's ten step corner radius scale. Components map to a step by how
    // round they should look, not by how big they are, and `full` is a real
    // half-height rounding rather than a large fixed number.
    readonly property int shapeNone: 0
    readonly property int shapeExtraSmall: 4
    readonly property int shapeSmall: 8
    readonly property int shapeMedium: 12
    readonly property int shapeLarge: 16
    readonly property int shapeLargeIncreased: 20
    readonly property int shapeExtraLarge: 28
    readonly property int shapeExtraLargeIncreased: 32
    readonly property int shapeExtraExtraLarge: 48
    function shapeFull(size) { return size/2 }
    // Nested shapes look unbalanced sharing a radius. Material subtracts the
    // padding between them instead.
    function shapeInside(outer,padding) { return Math.max(0,outer-padding) }

    // --- Elevation -----------------------------------------------------------
    // Material's six levels, and the two shadows it casts at each one: a tight
    // key light at 30% and a wider ambient at 15%. Levels are dp of elevation;
    // the shadows are the published values for that level.
    readonly property var elevationDp: [0,1,3,6,8,12]
    // [vertical offset, blur, spread] for the key shadow and then the ambient.
    readonly property var elevationKey: [[0,0,0],[1,2,0],[1,2,0],[1,3,0],[2,3,0],[4,4,0]]
    readonly property var elevationAmbient: [[0,0,0],[1,3,1],[2,6,2],[4,8,3],[6,10,4],[8,12,6]]
    readonly property real elevationKeyOpacity: 0.30
    readonly property real elevationAmbientOpacity: 0.15

    // --- Buttons -------------------------------------------------------------
    // Material's five button sizes. A size is not just a height: it carries its
    // own corner for the squarer pressed and selected states, its own icon size,
    // its own padding and its own gap between icon and label.
    // Material drops the small button to 36dp when a precision pointer is
    // driving it, and stops reserving the 48dp target that exists to
    // disambiguate touches.
    readonly property bool precisePointer: app.precisePointer
    readonly property int minimumTarget: precisePointer ? 0 : 48
    readonly property var buttonHeights: precisePointer ? ({xsmall:32, small:36, medium:56, large:96})
                                                        : ({xsmall:32, small:40, medium:56, large:96})
    readonly property var buttonSquare: ({xsmall:shapeMedium, small:shapeMedium, medium:shapeLarge, large:shapeExtraLarge})
    readonly property var buttonIcon: ({xsmall:20, small:20, medium:24, large:32})
    readonly property var buttonInset: ({xsmall:16, small:16, medium:24, large:48})
    readonly property var buttonGap: ({xsmall:8, small:8, medium:8, large:12})
    readonly property var buttonLabel: ({xsmall:labelLarge, small:labelLarge, medium:titleMedium, large:headlineSmall})
    // Material's optical centering: content inside an asymmetric shape is
    // nudged by this much of the difference between its two corner radii, so it
    // looks centred rather than measuring centred.
    readonly property real opticalCentering: 0.11
    function opticalShift(startRadius,endRadius) { return opticalCentering*(startRadius-endRadius) }

    // --- Motion --------------------------------------------------------------
    // Material replaced easing and duration with springs. Qt Quick animates on
    // curves, and the specification publishes the curve each spring converts to
    // for exactly this case, so the tokens below are those conversions.
    //
    // Two schemes. Expressive overshoots its target and settles back, which is
    // what gives it life; standard eases in without the bounce. Spatial springs
    // move things, so they may overshoot. Effects springs carry colour and
    // opacity, where overshooting would mean passing through a wrong value, so
    // they never do.
    readonly property bool expressiveMotion: app.motionScheme !== "standard"
    readonly property var springFastSpatial: expressiveMotion ? [0.42,1.67,0.21,0.90,1,1] : [0.27,1.06,0.18,1.00,1,1]
    readonly property var springSpatial: expressiveMotion ? [0.38,1.21,0.22,1.00,1,1] : [0.27,1.06,0.18,1.00,1,1]
    readonly property var springSlowSpatial: expressiveMotion ? [0.39,1.29,0.35,0.98,1,1] : [0.27,1.06,0.18,1.00,1,1]
    readonly property var springFastEffects: [0.31,0.94,0.34,1.00,1,1]
    readonly property var springEffects: [0.34,0.80,0.34,1.00,1,1]
    readonly property var springSlowEffects: [0.34,0.88,0.34,1.00,1,1]
    readonly property int springFastSpatialMs: app.motion ? 350 : 0
    readonly property int springSpatialMs: app.motion ? (expressiveMotion ? 500 : 500) : 0
    readonly property int springSlowSpatialMs: app.motion ? (expressiveMotion ? 650 : 750) : 0
    readonly property int springFastEffectsMs: app.motion ? 150 : 0
    readonly property int springEffectsMs: app.motion ? 200 : 0
    readonly property int springSlowEffectsMs: app.motion ? 300 : 0

    // --- Typography ----------------------------------------------------------
    readonly property string fontFamily: "Google Sans Flex"
    // Material's emphasized styles lean on a variable font's weight and width
    // to carry hierarchy, rather than only its size. Google Sans Flex is
    // variable, so the emphasis is real rather than a synthesised bold.
    // Material's emphasized styles are one weight step up from the regular
    // ones, and the step is not the same for every role: labels go from medium
    // to bold, everything else from regular to medium.
    readonly property int emphasizedWidth: 110
    readonly property int regularWidth: 100
    function weightFor(emphasized,label) {
        if (label) return emphasized ? Font.Bold : Font.Medium
        return emphasized ? Font.Medium : Font.Normal
    }
    readonly property int displaySmall: 36
    readonly property int headlineMedium: 28
    readonly property int headlineSmall: 24
    readonly property int titleLarge: 22
    readonly property int titleMedium: 16
    readonly property int rowHeight: app.viewCompactDensity ? 56 : 72
    readonly property int rowArtwork: app.viewCompactDensity ? 36 : 48
    readonly property int gridCell: app.viewCompactDensity ? 148 : 180
    readonly property int bodyLarge: 16
    readonly property int bodyMedium: 14
    readonly property int labelLarge: 14
    readonly property int labelMedium: 12
    readonly property int labelSmall: 11
    readonly property real hoverOpacity: 0.08
    readonly property real pressedOpacity: 0.10
    readonly property bool followDesktop: app.theme === "system" && desktopTheme.available
    readonly property bool dark: followDesktop ? desktopTheme.dark : app.theme === "dark" || (app.theme === "system" && Application.styleHints.colorScheme === Qt.Dark)
    readonly property color background: followDesktop ? desktopTheme.colors.background : role("background", dark ? "#181211" : "#fff8f6")
    readonly property color surface: followDesktop ? desktopTheme.colors.surface : role("surfaceContainerLow", dark ? "#201a18" : "#fff1ec")
    readonly property color container: followDesktop ? desktopTheme.colors.container : role("surfaceContainer", dark ? "#2b2320" : "#f6e5de")
    readonly property color high: followDesktop ? desktopTheme.colors.high : role("surfaceContainerHigh", dark ? "#382c28" : "#efddd5")
    readonly property color text: followDesktop ? desktopTheme.colors.text : role("onSurface", dark ? "#f5ded5" : "#281912")
    readonly property color muted: followDesktop ? desktopTheme.colors.muted : role("onSurfaceVariant", dark ? "#d5bfb5" : "#705c53")
    readonly property color outline: followDesktop ? desktopTheme.colors.outline : role("outlineVariant", dark ? "#57443b" : "#dcc5b9")
    // Controls need a stronger boundary than decorative surface dividers.
    readonly property color controlOutline: Qt.rgba(muted.r, muted.g, muted.b, dark ? 0.65 : 0.8)
    readonly property color primary: useSource ? role("primary",sourceColor) : followDesktop ? desktopTheme.colors.primary : (dark ? "#ffb596" : "#964829")
    readonly property color primaryText: useSource ? role("onPrimary",luminance(primary)>0.179?"#000000":"#ffffff") : followDesktop ? desktopTheme.colors.primaryText : (dark ? "#572008" : "#ffffff")
    readonly property color primaryContainer: useSource ? role("primaryContainer",blend(container,primary,0.16)) : followDesktop ? desktopTheme.colors.primaryContainer : (dark ? "#75351b" : "#ffdbcb")
    readonly property color containerText: useSource ? role("onPrimaryContainer",readable(primary,[primaryContainer])) : followDesktop ? desktopTheme.colors.containerText : (dark ? "#ffdbcb" : "#743419")
    readonly property color secondaryContainer: role("secondaryContainer", dark ? "#54432a" : "#f5e0bb")
    readonly property color secondary: followDesktop ? desktopTheme.colors.secondary : role("secondary", dark ? "#d8c4a0" : "#6c5b3b")
    // The inverse roles. A snackbar sits against the theme rather than in it,
    // so it takes the surface and the accent the other theme would have used.
    readonly property color inverseSurface: role("inverseSurface", dark ? "#f5ded5" : "#3c2c25")
    readonly property color inverseSurfaceText: role("inverseOnSurface", dark ? "#392e2a" : "#ffede6")
    readonly property color inversePrimary: role("inversePrimary", dark ? "#964829" : "#ffb596")
    readonly property color error: dark ? "#ffb4ab" : "#ba1a1a"
    readonly property color errorText: dark ? "#690005" : "#ffffff"
    readonly property color errorContainer: dark ? "#93000a" : "#ffdad6"
    readonly property color errorContainerText: dark ? "#ffdad6" : "#410002"
    // Material's fixed accents keep one tone in both themes, so anything drawn
    // with them holds its identity when the rest of the window flips.
    readonly property color primaryFixed: role("primaryFixed","#ffdbcb")
    readonly property color primaryFixedDim: role("primaryFixedDim","#ffb596")
    readonly property color primaryFixedText: role("onPrimaryFixed","#360f00")
    readonly property color primaryFixedVariantText: role("onPrimaryFixedVariant","#743419")
    // The names the rest of the application already uses, now resolved through
    // the spring tokens above rather than carrying their own numbers. Changing
    // the motion scheme therefore reaches every animation in the app at once.
    readonly property int fast: springFastEffectsMs
    readonly property int normal: springEffectsMs
    readonly property int slow: springSlowEffectsMs
    readonly property int enterDuration: springFastEffectsMs
    readonly property int exitDuration: app.motion ? 100 : 0
    readonly property var enterCurve: springFastEffects
    readonly property var exitCurve: [0.3,0,1,1,1,1]
    readonly property var fastSpatialCurve: springFastSpatial
    readonly property var effectsCurve: springEffects
    readonly property var fastEffectsCurve: springFastEffects
    readonly property var curve: springSpatial
}
