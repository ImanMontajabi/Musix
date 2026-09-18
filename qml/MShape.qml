import QtQuick
import QtQuick.Shapes

// One of Material's named shapes, filled. The outline comes from the same
// source the artwork mask and the loading indicator use, so a placeholder and
// the cover that replaces it are cut to exactly the same silhouette.
Shape {
    id: outline

    property string shape: "circle"
    property color color: Theme.high
    readonly property int sampleCount: 96

    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        fillColor: outline.color
        strokeColor: "transparent"
        PathPolyline {
            path: {
                const radii = app.shapeOutline(outline.shape, outline.sampleCount)
                const extent = Math.min(outline.width, outline.height)/2
                const cx = outline.width/2, cy = outline.height/2
                const points = []
                for (let i = 0; i < radii.length; ++i) {
                    const angle = i*2*Math.PI/(radii.length-1)
                    points.push(Qt.point(cx + radii[i]*extent*Math.cos(angle),
                                         cy + radii[i]*extent*Math.sin(angle)))
                }
                return points
            }
        }
    }
}
