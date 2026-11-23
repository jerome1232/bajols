using System.Globalization;
using System.Numerics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Bajols.Core;

namespace Bajols.GroundStation.Views;

/// <summary>
/// Perspective-projected three-dimensional submarine with a fixed camera south
/// of the vehicle. World axes are east/right, up, and north/into the screen.
/// </summary>
public sealed class SubmarineScene : Control
{
    /// <summary>Top space reserved for overlaid telemetry readouts.</summary>
    public static readonly StyledProperty<double> TopOverlayHeightProperty =
        AvaloniaProperty.Register<SubmarineScene, double>(nameof(TopOverlayHeight));

    /// <summary>Top overlay height, keeping compass and pose annotations below the readouts.</summary>
    public double TopOverlayHeight
    {
        get => GetValue(TopOverlayHeightProperty);
        set => SetValue(TopOverlayHeightProperty, value);
    }

    /// <summary>Finite top inset clamped to the scene's available height.</summary>
    private double TopInset => double.IsFinite(TopOverlayHeight) ? Math.Clamp(TopOverlayHeight, 0, Math.Max(0, Bounds.Height - 1)) : 0;

    /// <summary>
    /// Magnetic heading in clockwise degrees from north.
    /// </summary>
    public static readonly StyledProperty<double> HeadingProperty =
        AvaloniaProperty.Register<SubmarineScene, double>(nameof(Heading));

    /// <summary>
    /// Measured nose-up-positive pitch in degrees, independent of dive intent.
    /// </summary>
    public static readonly StyledProperty<double> PitchProperty =
        AvaloniaProperty.Register<SubmarineScene, double>(nameof(Pitch));

    /// <summary>
    /// Whether confirmed, fresh telemetry permits drawing the vehicle pose.
    /// </summary>
    public static readonly StyledProperty<bool> HasAttitudeProperty =
        AvaloniaProperty.Register<SubmarineScene, bool>(nameof(HasAttitude));

    /// <summary>
    /// Whether a fresh forward acquisition permits a schematic sensing guide.
    /// </summary>
    public static readonly StyledProperty<bool> HasForwardRangeProperty =
        AvaloniaProperty.Register<SubmarineScene, bool>(nameof(HasForwardRange));

    /// <summary>
    /// Current or historical display-facing range/bearing returns; no object shape or tracking is inferred.
    /// </summary>
    public static readonly StyledProperty<IReadOnlyList<ForwardRangeSample>> ForwardObstaclesProperty =
        AvaloniaProperty.Register<SubmarineScene, IReadOnlyList<ForwardRangeSample>>(nameof(ForwardObstacles), Array.Empty<ForwardRangeSample>());

    /// <summary>
    /// Continuous warning phase supplied by the presentation clock, in the range zero to one.
    /// </summary>
    public static readonly StyledProperty<double> ObstaclePulseProperty =
        AvaloniaProperty.Register<SubmarineScene, double>(nameof(ObstaclePulse));

    /// <summary>
    /// Fixed camera elevation above the south side of the compass plane.
    /// </summary>
    private const double Elevation = Math.PI * 25 / 180;

    /// <summary>
    /// Camera distance keeps every model vertex in front of the projection plane.
    /// </summary>
    private const double CameraDistance = 16;

    /// <summary>
    /// Fixed compass-plane height below the vehicle's body origin.
    /// </summary>
    private const float FloorHeight = -0.9f;

    /// <summary>
    /// Immutable low-poly model shared by all scene instances.
    /// </summary>
    private static readonly Facet[] Mesh = CreateMesh();

    /// <summary>
    /// Stationary floor/compass line styling.
    /// </summary>
    private static readonly Pen GridPen = new(new SolidColorBrush(Color.Parse("#294858")), 1);

    /// <summary>
    /// Emphasized fixed north/south axis styling.
    /// </summary>
    private static readonly Pen AxisPen = new(new SolidColorBrush(Color.Parse("#466676")), 1);

    /// <summary>
    /// Direction-label brush, separate from the telemetry-driven model.
    /// </summary>
    private static readonly IBrush LabelBrush = new SolidColorBrush(Color.Parse("#A9BAC8"));

    /// <summary>
    /// North label highlight consistent with the dashboard's provenance labels.
    /// </summary>
    private static readonly IBrush NorthBrush = new SolidColorBrush(Color.Parse("#F2C879"));

    /// <summary>
    /// Fixed world-space illumination from above the viewer's left shoulder.
    /// </summary>
    private static readonly Vector3 Light = Vector3.Normalize(new Vector3(-0.4f, 0.8f, -0.45f));

    /// <summary>
    /// One model-space polygon and its base material color.
    /// </summary>
    private sealed class Facet
    {
        /// <summary>
        /// Vertices in consistent outward winding; fixed after model construction.
        /// </summary>
        public Vector3[] Vertices { get; }

        /// <summary>
        /// Unlit material color.
        /// </summary>
        public Color Color { get; }

        /// <summary>
        /// Captures a model polygon without any dependency on telemetry.
        /// </summary>
        public Facet(Vector3[] vertices, Color color)
        {
            Vertices = vertices;
            Color = color;
        }
    }

    /// <summary>
    /// One projected polygon carrying depth and world-lit material for painter sorting.
    /// </summary>
    private sealed class ProjectedFacet
    {
        /// <summary>
        /// Closed screen-space geometry.
        /// </summary>
        public Geometry Shape { get; }

        /// <summary>
        /// Shaded material used for fill and matching antialias seam coverage.
        /// </summary>
        public IBrush Brush { get; }

        /// <summary>
        /// Mean camera-space distance; larger means farther away.
        /// </summary>
        public double Depth { get; }

        /// <summary>
        /// Captures a frame-local projected surface.
        /// </summary>
        public ProjectedFacet(Geometry shape, IBrush brush, double depth)
        {
            Shape = shape;
            Brush = brush;
            Depth = depth;
        }
    }

    /// <summary>
    /// Controls the tint and pixel rendering of the CRT display.
    /// </summary>
    public static readonly StyledProperty<ISolidColorBrush?> PhosphorProperty =
        AvaloniaProperty.Register<SubmarineScene, ISolidColorBrush?>(nameof(Phosphor));

    /// <summary>
    /// The CRT phosphor tint, or a transparent brush for the modern display.
    /// </summary>
    public ISolidColorBrush? Phosphor
    {
        get => GetValue(PhosphorProperty);
        set => SetValue(PhosphorProperty, value);
    }

    private Color? PhosphorColor => Phosphor is { Color.A: > 0 } brush ? brush.Color : null;

    private static readonly Pen ScanlinePen = new(new SolidColorBrush(Color.FromArgb(42, 0, 0, 0)), 1);

    static SubmarineScene()
    {
        AffectsRender<SubmarineScene>(HeadingProperty, PitchProperty, HasAttitudeProperty,
            HasForwardRangeProperty, ForwardObstaclesProperty, ObstaclePulseProperty, TopOverlayHeightProperty, PhosphorProperty);
    }

    /// <summary>
    /// Clockwise magnetic heading, with zero pointing away from the viewer.
    /// </summary>
    public double Heading
    {
        get => GetValue(HeadingProperty);
        set => SetValue(HeadingProperty, value);
    }

    /// <summary>
    /// Nose-up-positive measured pitch; this does not command the vehicle.
    /// </summary>
    public double Pitch
    {
        get => GetValue(PitchProperty);
        set => SetValue(PitchProperty, value);
    }

    /// <summary>
    /// Whether a vehicle pose may be shown without inventing unavailable telemetry.
    /// </summary>
    public bool HasAttitude
    {
        get => GetValue(HasAttitudeProperty);
        set => SetValue(HasAttitudeProperty, value);
    }

    /// <summary>
    /// Fresh sensor acquisition availability; independent of detecting an object.
    /// </summary>
    public bool HasForwardRange
    {
        get => GetValue(HasForwardRangeProperty);
        set => SetValue(HasForwardRangeProperty, value);
    }

    /// <summary>
    /// Current or historical bearing/range returns filtered by the presentation layer; at most 64 are drawn.
    /// </summary>
    public IReadOnlyList<ForwardRangeSample> ForwardObstacles
    {
        get => GetValue(ForwardObstaclesProperty);
        set => SetValue(ForwardObstaclesProperty, value);
    }

    /// <summary>
    /// Warning brightness phase; the marker remains visible even at its dimmest point.
    /// </summary>
    public double ObstaclePulse
    {
        get => GetValue(ObstaclePulseProperty);
        set => SetValue(ObstaclePulseProperty, value);
    }

    /// <summary>
    /// Projects a clearance schematically ahead of the bow, following heading and pitch.
    /// The uncalibrated model has no physical scale; this is not a mapped object position.
    /// Edge-clamping keeps the warning visible at steep pitch instead of hiding it offscreen.
    /// </summary>
    public Point ProjectForwardObstacle(double distanceMillimetres, double bearingDegrees = 0)
    {
        if (!double.IsFinite(distanceMillimetres) || distanceMillimetres < 0 || distanceMillimetres > 6000)
        {
            throw new ArgumentOutOfRangeException(nameof(distanceMillimetres));
        }

        if (!double.IsFinite(bearingDegrees) || Math.Abs(bearingDegrees) > 90)
        {
            throw new ArgumentOutOfRangeException(nameof(bearingDegrees));
        }

        double bearing = bearingDegrees * Math.PI / 180;
        double distance = distanceMillimetres / 1000 * 3.4;
        Point point = ProjectBodyPoint(Math.Sin(bearing) * distance, 0, 2.52 + Math.Cos(bearing) * distance);
        return new Point(
            Math.Clamp(point.X, Math.Min(32, Bounds.Width / 2), Math.Max(Bounds.Width - 32, Bounds.Width / 2)),
            Math.Clamp(point.Y, Math.Min(TopInset + 32, (Bounds.Height + TopInset) / 2), Math.Max(Bounds.Height - 32, (Bounds.Height + TopInset) / 2)));
    }

    /// <summary>
    /// Projects a body-relative point for pose-aligned annotations. Positive axes
    /// are starboard, up and ahead; heading/pitch rotate the body, never the camera.
    /// </summary>
    public Point ProjectBodyPoint(double starboard, double up, double ahead)
    {
        return Project(Vector3.Transform(new Vector3((float)starboard, (float)up, (float)ahead), Rotation()));
    }

    /// <summary>
    /// Draws the fixed compass plane and a live or static submarine model.
    /// </summary>
    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            return;
        }

        Color? phosphor = PhosphorColor;
        using (context.PushRenderOptions(new RenderOptions { EdgeMode = phosphor.HasValue ? EdgeMode.Aliased : EdgeMode.Unspecified }))
        using (context.PushClip(new Rect(Bounds.Size)))
        {
            DrawCompass(context);
            if (HasAttitude)
            {
                if (HasForwardRange)
                {
                    DrawForwardGuide(context);
                }

                DrawVehicle(context, Rotation());
                if (ForwardObstacles is { } observations)
                {
                    for (int i = 0; i < Math.Min(64, observations.Count); i++)
                    {
                        ForwardRangeSample sample = observations[i];
                        if (sample.State == ForwardRangeState.Detected && sample.DistanceMillimetres is >= 0 and <= 6000 &&
                            sample.BearingDegrees is double bearing && Math.Abs(bearing) <= 90)
                        {
                            DrawForwardObstacle(context, sample);
                        }
                    }
                }
            }
            else
            {
                using (context.PushOpacity(0.55))
                {
                    DrawVehicle(context, Quaternion.CreateFromAxisAngle(Vector3.UnitY, MathF.PI / 2));
                }
            }

            if (phosphor.HasValue)
            {
                for (double y = TopInset + 0.5; y < Bounds.Height; y += 4)
                {
                    context.DrawLine(ScanlinePen, new Point(0, y), new Point(Bounds.Width, y));
                }
            }

            DrawLabels(context);
        }
    }

    /// <summary>
    /// Draws a single forward ray, without inventing a field of view or free-space volume.
    /// </summary>
    private void DrawForwardGuide(DrawingContext context)
    {
        Point bow = ProjectBodyPoint(0, 0, 2.52);
        Point extent = ProjectForwardObstacle(1000);
        var pen = new Pen(new SolidColorBrush(Color.Parse("#627F8F")), 1, DashStyle.Dash);
        context.DrawLine(pen, bow, extent);
        context.DrawEllipse(null, pen, extent, 3, 3);
    }

    /// <summary>
    /// Draws a persistent return marker with a flashing halo, smoothly increasing
    /// size, opacity, stroke and amber-to-red color as the six-metre display range closes.
    /// This is a sensor-return annotation, not an inferred object mesh or physical size.
    /// </summary>
    private void DrawForwardObstacle(DrawingContext context, ForwardRangeSample sample)
    {
        double distance = sample.DistanceMillimetres!.Value;
        double proximity = 1 - distance / 6000;
        double phase = !sample.IsStale && double.IsFinite(ObstaclePulse) ? Math.Clamp(ObstaclePulse, 0, 1) : 0;
        double radius = 6 + 11 * proximity;
        double brightness = sample.IsStale ? 0.65 - 0.25 * sample.StaleFade : (0.7 + 0.3 * proximity) * (0.5 + 0.5 * phase);
        byte red = (byte)(242 + (241 - 242) * proximity);
        byte green = (byte)(200 + (73 - 200) * proximity);
        byte blue = (byte)(121 + (89 - 121) * proximity);
        red = (byte)(red + (220 - red) * sample.StaleFade);
        green = (byte)(green + (225 - green) * sample.StaleFade);
        blue = (byte)(blue + (230 - blue) * sample.StaleFade);
        Point center = ProjectForwardObstacle(distance, sample.BearingDegrees!.Value);
        var stroke = new SolidColorBrush(Color.FromArgb((byte)(255 * brightness), red, green, blue));
        var fill = new SolidColorBrush(Color.FromArgb((byte)(110 * brightness), red, green, blue));
        var halo = new SolidColorBrush(Color.FromArgb((byte)(55 * brightness), red, green, blue));
        double haloRadius = radius * (1.7 + 0.35 * phase);
        if (!sample.IsStale)
        {
            context.DrawEllipse(halo, null, center, haloRadius, haloRadius);
        }

        context.DrawEllipse(fill, new Pen(stroke, 1.5 + 2 * proximity), center, radius, radius);
        context.DrawLine(new Pen(stroke, 1.5), new Point(center.X - 3, center.Y), new Point(center.X + 3, center.Y));
        context.DrawLine(new Pen(stroke, 1.5), new Point(center.X, center.Y - 3), new Point(center.X, center.Y + 3));
        Point label = center.Y <= TopInset + 33
            ? new Point(center.X + radius + 36, center.Y)
            : new Point(center.X, center.Y - radius - 11);
        DrawLabel(context, $"{distance / 1000:0.##} m{(sample.IsStale ? " · stale" : "")}", label, stroke);
    }

    /// <summary>
    /// Creates world orientation with heading about vertical and pitch about local starboard.
    /// </summary>
    private Quaternion Rotation()
    {
        double heading = double.IsFinite(Heading) ? Heading % 360 : 0;
        double pitch = double.IsFinite(Pitch) ? Pitch % 360 : 0;
        return Quaternion.CreateFromAxisAngle(Vector3.UnitY, (float)(heading * Math.PI / 180)) *
            Quaternion.CreateFromAxisAngle(Vector3.UnitX, (float)(-pitch * Math.PI / 180));
    }

    /// <summary>
    /// Calculates fixed-camera depth increasing northward and away from the viewer.
    /// </summary>
    private static double Depth(Vector3 world)
    {
        return world.Z * Math.Cos(Elevation) - world.Y * Math.Sin(Elevation);
    }

    /// <summary>
    /// Applies perspective and elevated-camera projection without rotating compass directions.
    /// </summary>
    private Point Project(Vector3 world)
    {
        double sceneHeight = Bounds.Height - TopInset;
        double scale = Math.Min(Bounds.Width / 9, sceneHeight / 7);
        // Keep distant annotations in front of the projection plane before edge clamping.
        double perspective = CameraDistance / Math.Max(1, CameraDistance + Depth(world));
        double vertical = world.Y * Math.Cos(Elevation) + world.Z * Math.Sin(Elevation);
        return new Point(Bounds.Width / 2 + world.X * scale * perspective,
            TopInset + sceneHeight * 0.46 - vertical * scale * perspective);
    }

    /// <summary>
    /// Draws the stationary perspective compass ring and floor grid below the submarine.
    /// </summary>
    private void DrawCompass(DrawingContext context)
    {
        const float radius = 3.2f;
        Pen gridPen = GridPen;
        Pen axisPen = AxisPen;
        if (PhosphorColor is { } phosphor)
        {
            gridPen = new Pen(new SolidColorBrush(phosphor, 0.25), 1);
            axisPen = new Pen(new SolidColorBrush(phosphor, 0.45), 1);
        }
        for (int i = 0; i < 64; i++)
        {
            double a = i * 2 * Math.PI / 64;
            double b = (i + 1) * 2 * Math.PI / 64;
            context.DrawLine(gridPen,
                Project(new Vector3((float)Math.Cos(a) * radius, FloorHeight, (float)Math.Sin(a) * radius)),
                Project(new Vector3((float)Math.Cos(b) * radius, FloorHeight, (float)Math.Sin(b) * radius)));
        }

        for (int i = -2; i <= 2; i++)
        {
            context.DrawLine(gridPen, Project(new Vector3(i, FloorHeight, -2.5f)),
                Project(new Vector3(i, FloorHeight, 2.5f)));
            context.DrawLine(gridPen, Project(new Vector3(-2.5f, FloorHeight, i)),
                Project(new Vector3(2.5f, FloorHeight, i)));
        }

        context.DrawLine(axisPen, Project(new Vector3(0, FloorHeight, -radius)),
            Project(new Vector3(0, FloorHeight, radius)));
        context.DrawLine(axisPen, Project(new Vector3(-radius, FloorHeight, 0)),
            Project(new Vector3(radius, FloorHeight, 0)));
    }

    /// <summary>
    /// Labels fixed compass directions; N is far/into screen, S is near/toward viewer.
    /// </summary>
    private void DrawLabels(DrawingContext context)
    {
        Point north = Project(new Vector3(0, FloorHeight, 3.2f));
        Point south = Project(new Vector3(0, FloorHeight, -3.2f));
        Point west = Project(new Vector3(-3.2f, FloorHeight, 0));
        Point east = Project(new Vector3(3.2f, FloorHeight, 0));
        IBrush labelBrush = PhosphorColor.HasValue ? Phosphor! : LabelBrush;
        DrawLabel(context, "N", new Point(north.X, TopInset + 10), PhosphorColor.HasValue ? labelBrush : NorthBrush);
        DrawLabel(context, "S", new Point(south.X, Bounds.Height - 10), labelBrush);
        DrawLabel(context, "W", new Point(west.X - 16, west.Y), labelBrush);
        DrawLabel(context, "E", new Point(east.X + 16, east.Y), labelBrush);
    }

    /// <summary>
    /// Centers a short compass label in screen space using the dashboard font.
    /// </summary>
    private void DrawLabel(DrawingContext context, string label, Point center, IBrush brush)
    {
        var text = new FormattedText(label, CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
            PhosphorColor.HasValue ? new Typeface(StationThemes.Monospace) : Typeface.Default, 12, brush);
        context.DrawText(text, new Point(center.X - text.Width / 2, center.Y - text.Height / 2));
    }

    /// <summary>
    /// Rotates actual model vertices, computes lit normals, and paints far surfaces before near ones.
    /// </summary>
    private void DrawVehicle(DrawingContext context, Quaternion rotation)
    {
        var projected = new List<ProjectedFacet>(Mesh.Length);
        Color? phosphor = PhosphorColor;

        Point ProjectHull(Vector3 vertex)
        {
            Point point = Project(vertex);
            const double pixel = 3;
            return phosphor.HasValue
                ? new Point(Math.Round(point.X / pixel) * pixel, Math.Round(point.Y / pixel) * pixel)
                : point;
        }

        foreach (Facet facet in Mesh)
        {
            Vector3[] world = facet.Vertices.Select(vertex => Vector3.Transform(vertex, rotation)).ToArray();
            Vector3 normal = Vector3.Normalize(Vector3.Cross(world[1] - world[0], world[2] - world[0]));
            double illumination = 0.3 + 0.7 * Math.Max(0, Vector3.Dot(normal, Light));
            Color color = facet.Color;
            if (phosphor is { } tint)
            {
                illumination = Math.Round(illumination * 5) / 5 * color.G / 255;
                color = tint;
            }
            var brush = new SolidColorBrush(Color.FromRgb((byte)(color.R * illumination),
                (byte)(color.G * illumination), (byte)(color.B * illumination)));
            var geometry = new StreamGeometry();
            using (StreamGeometryContext path = geometry.Open())
            {
                path.BeginFigure(ProjectHull(world[0]), true);
                foreach (Vector3 vertex in world.Skip(1))
                {
                    path.LineTo(ProjectHull(vertex));
                }

                path.EndFigure(true);
            }

            projected.Add(new ProjectedFacet(geometry, brush, world.Average(Depth)));
        }

        projected.Sort(CompareDepth);
        foreach (ProjectedFacet facet in projected)
        {
            context.DrawGeometry(facet.Brush, new Pen(facet.Brush, 0.5), facet.Shape);
        }
    }

    /// <summary>
    /// Orders projected surfaces back-to-front in the fixed camera coordinate system.
    /// </summary>
    private static int CompareDepth(ProjectedFacet left, ProjectedFacet right)
    {
        return right.Depth.CompareTo(left.Depth);
    }

    /// <summary>
    /// Builds a rounded hull, sail, periscope, dive planes, rudder and propeller in 3D.
    /// </summary>
    private static Facet[] CreateMesh()
    {
        var facets = new List<Facet>();
        Color hull = Color.Parse("#69CCC4");
        Color fins = Color.Parse("#3E9EAA");
        Color metal = Color.Parse("#A8DDE1");
        (float Z, float Radius)[] rings = [(-2.5f, 0.06f), (-2.15f, 0.27f),
            (-1.55f, 0.43f), (-0.7f, 0.47f), (1.2f, 0.47f), (1.85f, 0.38f),
            (2.22f, 0.24f), (2.47f, 0.06f)];
        for (int ring = 0; ring < rings.Length - 1; ring++)
        {
            for (int segment = 0; segment < 16; segment++)
            {
                double a = segment * 2 * Math.PI / 16;
                double b = (segment + 1) * 2 * Math.PI / 16;
                facets.Add(new Facet([RingPoint(rings[ring], a), RingPoint(rings[ring], b),
                    RingPoint(rings[ring + 1], b), RingPoint(rings[ring + 1], a)], hull));
            }
        }

        for (int segment = 0; segment < 16; segment++)
        {
            double a = segment * 2 * Math.PI / 16;
            double b = (segment + 1) * 2 * Math.PI / 16;
            facets.Add(new Facet([new Vector3(0, 0, -2.55f), RingPoint(rings[0], b), RingPoint(rings[0], a)], metal));
            facets.Add(new Facet([new Vector3(0, 0, 2.52f), RingPoint(rings[^1], a), RingPoint(rings[^1], b)], hull));
        }

        AddBox(facets, new Vector3(-0.2f, 0.36f, -0.2f), new Vector3(0.2f, 0.95f, 0.65f), fins);
        AddBox(facets, new Vector3(-0.035f, 0.95f, 0.22f), new Vector3(0.035f, 1.32f, 0.29f), metal);
        AddBox(facets, new Vector3(-0.035f, 1.25f, 0.22f), new Vector3(0.035f, 1.32f, 0.55f), metal);
        AddBox(facets, new Vector3(-1.05f, -0.035f, 0.9f), new Vector3(1.05f, 0.035f, 1.15f), fins);
        AddBox(facets, new Vector3(-0.9f, -0.035f, -2.1f), new Vector3(0.9f, 0.035f, -1.65f), fins);
        AddBox(facets, new Vector3(-0.035f, -0.65f, -2.3f), new Vector3(0.035f, 0.85f, -1.75f), fins);
        AddBox(facets, new Vector3(-0.06f, -0.06f, -2.9f), new Vector3(0.06f, 0.06f, -2.45f), metal);
        for (int blade = 0; blade < 3; blade++)
        {
            double a = blade * 2 * Math.PI / 3;
            Vector3 root = new((float)Math.Cos(a) * 0.06f, (float)Math.Sin(a) * 0.06f, -2.93f);
            Vector3 tip = new((float)Math.Cos(a + 0.5) * 0.43f, (float)Math.Sin(a + 0.5) * 0.43f, -2.93f);
            Vector3 edge = new((float)Math.Cos(a + 1.1) * 0.38f, (float)Math.Sin(a + 1.1) * 0.38f, -2.93f);
            facets.Add(new Facet([root, tip, edge], metal));
        }

        return facets.ToArray();
    }

    /// <summary>
    /// Calculates one circular cross-section vertex for the rounded 3D hull.
    /// </summary>
    private static Vector3 RingPoint((float Z, float Radius) ring, double angle)
    {
        return new Vector3((float)Math.Cos(angle) * ring.Radius, (float)Math.Sin(angle) * ring.Radius, ring.Z);
    }

    /// <summary>
    /// Adds six outward-wound box surfaces for appendages with real thickness and occlusion.
    /// </summary>
    private static void AddBox(List<Facet> facets, Vector3 min, Vector3 max, Color color)
    {
        Vector3 a = new(min.X, min.Y, min.Z);
        Vector3 b = new(max.X, min.Y, min.Z);
        Vector3 c = new(max.X, max.Y, min.Z);
        Vector3 d = new(min.X, max.Y, min.Z);
        Vector3 e = new(min.X, min.Y, max.Z);
        Vector3 f = new(max.X, min.Y, max.Z);
        Vector3 g = new(max.X, max.Y, max.Z);
        Vector3 h = new(min.X, max.Y, max.Z);
        facets.Add(new Facet([a, d, c, b], color));
        facets.Add(new Facet([e, f, g, h], color));
        facets.Add(new Facet([a, e, h, d], color));
        facets.Add(new Facet([b, c, g, f], color));
        facets.Add(new Facet([d, h, g, c], color));
        facets.Add(new Facet([a, b, f, e], color));
    }
}
