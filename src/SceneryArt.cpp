// The scenery's sprite page: pumpkins (bodies, the light through their cuts,
// curls of vine and big leaves), scarecrows (bodies, heads, the light through
// their cuts, hats, kerchief tails and straw hands), and the shared soft
// blobs, glows and litter leaves.
#include "SceneryParts.h"

using namespace scenery;

namespace {

using Pts = std::vector<D2D1_POINT_2F>;
using Cut = std::vector<Pts>;  // one cut through the rind: its outline (and an island of rind left in it)

// Gradient stop collections, one per distinct list of stops: a page draws
// hundreds of gradients from a few dozen lists, and a new collection costs far
// more to draw with than a new brush on one already used.
class Stops {
public:
    explicit Stops(ID2D1RenderTarget* rt) : rt_(rt) {}
    ID2D1RenderTarget* Target() const { return rt_; }
    ID2D1GradientStopCollection* Get(std::initializer_list<D2D1_GRADIENT_STOP> list) {
        for (const Entry& e : entries_)
            if (std::equal(e.stops.begin(), e.stops.end(), list.begin(), list.end(), Same)) return e.coll.Get();
        Entry& e = entries_.emplace_back();
        e.stops.assign(list.begin(), list.end());
        rt_->CreateGradientStopCollection(list.begin(), static_cast<UINT32>(list.size()), &e.coll);
        return e.coll.Get();
    }

private:
    static bool Same(const D2D1_GRADIENT_STOP& a, const D2D1_GRADIENT_STOP& b) {
        return a.position == b.position && a.color.r == b.color.r && a.color.g == b.color.g &&
               a.color.b == b.color.b && a.color.a == b.color.a;
    }
    struct Entry {
        std::vector<D2D1_GRADIENT_STOP> stops;
        ComPtr<ID2D1GradientStopCollection> coll;
    };
    ID2D1RenderTarget* rt_;
    std::vector<Entry> entries_;
};

D2D1_GRADIENT_STOP Stop(float at, Rgb c, float a = 1.0f) { return {at, Color(c, a)}; }

Vec3 Norm(Vec3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0.0f ? Vec3{v.x / l, v.y / l, v.z / l} : v;
}
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Light from the upper left and in front, like the pile's.
const Vec3 kLight = Norm({-0.48f, 0.62f, 0.62f});
const Vec3 kHalf = Norm({kLight.x, kLight.y, kLight.z + 1.0f});

D2D1_POINT_2F Add(D2D1_POINT_2F a, float x, float y) { return {a.x + x, a.y + y}; }
D2D1_POINT_2F LerpPt(D2D1_POINT_2F a, D2D1_POINT_2F b, float t) { return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)}; }

// A colour at brightness v: 0 its darkest .. 0.5 its middle .. 1 lit (a
// little past it brighter still), in steps, so pumpkins share gradient stops.
Rgb Tone(const Shade3& c, float v) {
    v = std::round(Clamp(v, 0.0f, 1.2f) * 25.0f) / 25.0f;
    const Rgb r = v < 0.5f ? Mix(c.dark, c.mid, v / 0.5f) : Mix(c.mid, c.lit, (v - 0.5f) / 0.5f);
    return {Saturate(r.r), Saturate(r.g), Saturate(r.b)};
}

float Area(const Pts& p) {
    float a = 0.0f;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += p[j].x * p[i].y - p[i].x * p[j].y;
    return 0.5f * a;
}

Pts Ellipse(D2D1_POINT_2F c, float rx, float ry, int n, float turn = 0.0f) {
    Pts p;
    const float ct = std::cos(turn), st = std::sin(turn);
    for (int i = 0; i < n; ++i) {
        const float a = kTau * static_cast<float>(i) / static_cast<float>(n);
        const float x = rx * std::cos(a), y = ry * std::sin(a);
        p.push_back({c.x + x * ct - y * st, c.y + x * st + y * ct});
    }
    return p;
}

// A smooth curve through `pts` (Catmull-Rom), `per` points per span.
Pts Smooth(const Pts& pts, int per, bool closed) {
    Pts out;
    const int n = static_cast<int>(pts.size());
    if (n < 3) return pts;
    auto at = [&](int i) {
        if (closed) return pts[static_cast<size_t>((i % n + n) % n)];
        return pts[static_cast<size_t>(Clamp(i, 0, n - 1))];
    };
    const int spans = closed ? n : n - 1;
    for (int i = 0; i < spans; ++i) {
        const D2D1_POINT_2F p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        for (int k = 0; k < per; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(per), t2 = t * t, t3 = t2 * t;
            auto f = [&](float a, float b, float c, float d) {
                return 0.5f * (2.0f * b + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 +
                               (-a + 3.0f * b - 3.0f * c + d) * t3);
            };
            out.push_back({f(p0.x, p1.x, p2.x, p3.x), f(p0.y, p1.y, p2.y, p3.y)});
        }
    }
    if (!closed) out.push_back(pts.back());
    return out;
}

// The outline of a stroke along `spine` whose width at each point is `w[i]`.
Pts Tube(const Pts& spine, const std::vector<float>& w) {
    Pts left, right;
    const size_t n = spine.size();
    for (size_t i = 0; i < n; ++i) {
        const D2D1_POINT_2F a = spine[i > 0 ? i - 1 : 0], b = spine[i + 1 < n ? i + 1 : n - 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        const float l = std::sqrt(dx * dx + dy * dy);
        if (l > 0.0f) {
            dx /= l;
            dy /= l;
        }
        const float h = 0.5f * w[i];
        left.push_back({spine[i].x + dy * h, spine[i].y - dx * h});
        right.push_back({spine[i].x - dy * h, spine[i].y + dx * h});
    }
    left.insert(left.end(), right.rbegin(), right.rend());
    return left;
}

// The page's drawing tools, in cell px.
struct Canvas {
    ID2D1DeviceContext* dc;
    ID2D1Factory* factory;
    ID2D1SolidColorBrush* brush;
    ID2D1StrokeStyle* round;
    Stops* stops;
    float s;     // px per DIP
    float hair;  // the finest line that still reads (at least a pixel)

    ComPtr<ID2D1PathGeometry> Figures(const std::vector<Pts>& figs, bool closed, bool alternate = false) const {
        ComPtr<ID2D1PathGeometry> geo;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return nullptr;
        sink->SetFillMode(alternate ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
        bool any = false;
        for (const Pts& pts : figs) {
            if (pts.size() < 2) continue;
            sink->BeginFigure(pts[0], closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLines(pts.data() + 1, static_cast<UINT32>(pts.size() - 1));
            sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            any = true;
        }
        if (FAILED(sink->Close()) || !any) return nullptr;
        return geo;
    }
    ComPtr<ID2D1PathGeometry> Path(const Pts& pts, bool closed) const { return Figures({pts}, closed); }
    ComPtr<ID2D1Brush> Linear(D2D1_POINT_2F a, D2D1_POINT_2F b, std::initializer_list<D2D1_GRADIENT_STOP> list) const {
        ComPtr<ID2D1LinearGradientBrush> br;
        if (ID2D1GradientStopCollection* coll = stops->Get(list))
            dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), coll, &br);
        return br;
    }
    // Lit to dark, from a to b.
    ComPtr<ID2D1Brush> Shade(D2D1_POINT_2F a, D2D1_POINT_2F b, const Shade3& c, float mid = 0.5f) const {
        return Linear(a, b, {Stop(0.0f, c.lit), Stop(mid, c.mid), Stop(1.0f, c.dark)});
    }
    ComPtr<ID2D1Brush> Radial(D2D1_POINT_2F c, D2D1_POINT_2F offset, float rx, float ry,
                              std::initializer_list<D2D1_GRADIENT_STOP> list) const {
        ComPtr<ID2D1RadialGradientBrush> br;
        if (ID2D1GradientStopCollection* coll = stops->Get(list))
            dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(c, offset, rx, ry), coll, &br);
        return br;
    }
    void Fill(ID2D1Geometry* g, Rgb c, float a) const {
        if (!g) return;
        brush->SetColor(Color(c, a));
        dc->FillGeometry(g, brush);
    }
    void Fill(ID2D1Geometry* g, ID2D1Brush* b) const {
        if (g && b) dc->FillGeometry(g, b);
    }
    void Stroke(ID2D1Geometry* g, Rgb c, float a, float w) const {
        if (!g) return;
        brush->SetColor(Color(c, a));
        dc->DrawGeometry(g, brush, w, round);
    }
    void Stroke(ID2D1Geometry* g, ID2D1Brush* b, float w) const {
        if (g && b) dc->DrawGeometry(g, b, w, round);
    }
    void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, Rgb c, float alpha, float w) const {
        brush->SetColor(Color(c, alpha));
        dc->DrawLine(a, b, brush, w, round);
    }
    // A polyline, stroked.
    void Lines(const Pts& pts, Rgb c, float alpha, float w) const {
        if (auto g = Path(pts, false)) Stroke(g.Get(), c, alpha, w);
    }
    void Dot(D2D1_POINT_2F c, float r, Rgb col, float a) const { Oval(c, r, r, col, a); }
    void Oval(D2D1_POINT_2F c, float rx, float ry, Rgb col, float a) const {
        brush->SetColor(Color(col, a));
        dc->FillEllipse(D2D1::Ellipse(c, rx, ry), brush);
    }
    void Rect(const D2D1_RECT_F& r, Rgb c, float a) const {
        brush->SetColor(Color(c, a));
        dc->FillRectangle(r, brush);
    }
    void Rect(const D2D1_RECT_F& r, ID2D1Brush* b) const {
        if (b) dc->FillRectangle(r, b);
    }
    // A shape filled shifted by (dx, dy): soft shadows reuse the shapes that cast them.
    void Shifted(ID2D1Geometry* g, float dx, float dy, Rgb c, float a) const {
        if (!g) return;
        D2D1::Matrix3x2F was;
        dc->GetTransform(&was);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy) * was);
        Fill(g, c, a);
        dc->SetTransform(was);
    }
    void Shifted(ID2D1Geometry* g, float dx, float dy, ID2D1Brush* b) const {
        if (!g || !b) return;
        D2D1::Matrix3x2F was;
        dc->GetTransform(&was);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy) * was);
        dc->FillGeometry(g, b);
        dc->SetTransform(was);
    }
    // A soft shadow behind a shape, down and to the right: keeps it apart from mid-toned wallpapers.
    void Shadow(ID2D1Geometry* g, float alpha) const {
        for (int i = 0; i < 3; ++i) {
            const float o = (0.7f + 0.35f * static_cast<float>(i)) * s;
            Shifted(g, o, o, kShadow, alpha);
        }
    }
    void Clip(ID2D1Geometry* g) const { dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), g), nullptr); }
    void Unclip() const { dc->PopLayer(); }
    // The union of shapes (they must all wind the same way).
    ComPtr<ID2D1Geometry> Union(const std::vector<ComPtr<ID2D1PathGeometry>>& parts) const {
        std::vector<ID2D1Geometry*> raw;
        for (const auto& p : parts)
            if (p) raw.push_back(p.Get());
        if (raw.empty()) return nullptr;
        ComPtr<ID2D1GeometryGroup> group;
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, raw.data(), static_cast<UINT32>(raw.size()), &group)) ||
            FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)) ||
            FAILED(group->Outline(nullptr, sink.Get())) || FAILED(sink->Close()))
            return group;
        return path;
    }
};

// ---------------------------------------------------------------------------
// Pumpkins
// ---------------------------------------------------------------------------

// A pumpkin's body about `c` (cell px, y down): each rib a lobe shaded on its
// own, drawn back to front, deepening into dark grooves; the upper left lit,
// the underside in shade, and a waxy sheen on the lobes facing the light.
// Heirlooms are striped green along their grooves. Returns the outline.
ComPtr<ID2D1Geometry> PaintGourd(const Canvas& cv, const Gourd& g, D2D1_POINT_2F c, const Shade3& rind, bool striped) {
    constexpr int M = 30, K = 8;
    struct Lobe {
        std::vector<ComPtr<ID2D1PathGeometry>> pieces;
        size_t main = 0;  // the biggest piece
        float depth = 0.0f, tone = 0.5f, spec = 0.0f, th = 0.0f;
        D2D1_RECT_F box = {1e9f, 1e9f, -1e9f, -1e9f};
    };
    std::vector<Lobe> lobes;
    std::vector<ComPtr<ID2D1PathGeometry>> all;
    const float step = kTau / static_cast<float>(g.ribs);
    D2D1_POINT_2F q[M + 1][K + 1];
    bool facing[M + 1][K + 1];
    float ps[M + 1];
    for (int i = 0; i <= M; ++i) ps[i] = kPi * (0.015f + 0.97f * static_cast<float>(i) / static_cast<float>(M));
    for (int j = 0; j < g.ribs; ++j) {
        const float th0 = g.turn + step * static_cast<float>(j);
        for (int i = 0; i <= M; ++i)
            for (int k = 0; k <= K; ++k) {
                const Vec3 p = g.At(th0 + step * static_cast<float>(k) / static_cast<float>(K), ps[i]);
                q[i][k] = {c.x + p.x, c.y - p.y};
            }
        // Which samples face the viewer: the grid keeps its turn on screen.
        for (int i = 0; i <= M; ++i)
            for (int k = 0; k <= K; ++k) {
                const D2D1_POINT_2F a = q[i][std::min(k + 1, K)], b = q[i][std::max(k - 1, 0)];
                const D2D1_POINT_2F u = q[std::min(i + 1, M)][k], d = q[std::max(i - 1, 0)][k];
                facing[i][k] = (a.x - b.x) * (u.y - d.y) - (a.y - b.y) * (u.x - d.x) > 0.0f;
            }
        Lobe lobe;
        lobe.th = th0 + 0.5f * step;
        lobe.depth = g.At(lobe.th, 0.5f * kPi).z;
        // Runs of rows with something showing, each a piece: down the left
        // edge of what shows, back up the right.
        float sumPs = 0.0f;
        int seen = 0;
        float biggest = 0.0f;
        for (int i = 0; i <= M;) {
            if (!std::any_of(facing[i], facing[i] + K + 1, [](bool f) { return f; })) {
                ++i;
                continue;
            }
            Pts left, right;
            for (; i <= M; ++i) {
                int lo = -1, hi = -1;
                for (int k = 0; k <= K; ++k) {
                    if (!facing[i][k]) continue;
                    if (lo < 0 || q[i][k].x < q[i][lo].x) lo = k;
                    if (hi < 0 || q[i][k].x > q[i][hi].x) hi = k;
                }
                if (lo < 0) break;
                left.push_back(q[i][lo]);
                right.push_back(q[i][hi]);
                if (facing[i][K / 2]) {
                    sumPs += ps[i];
                    ++seen;
                }
            }
            if (left.size() < 2) continue;
            left.insert(left.end(), right.rbegin(), right.rend());
            const float area = Area(left);
            if (std::fabs(area) < 0.5f) continue;
            if (area < 0.0f) std::reverse(left.begin(), left.end());  // all wind the same way, for the union
            for (const D2D1_POINT_2F& p : left) {
                lobe.box.left = std::min(lobe.box.left, p.x);
                lobe.box.top = std::min(lobe.box.top, p.y);
                lobe.box.right = std::max(lobe.box.right, p.x);
                lobe.box.bottom = std::max(lobe.box.bottom, p.y);
            }
            if (std::fabs(area) > biggest) {
                biggest = std::fabs(area);
                lobe.main = lobe.pieces.size();
            }
            lobe.pieces.push_back(cv.Path(left, true));
            all.push_back(lobe.pieces.back());
        }
        if (lobe.pieces.empty()) continue;
        // Lit by how its middle faces the light (where it shows).
        const Vec3 n = g.Normal(lobe.th, seen ? sumPs / static_cast<float>(seen) : 0.5f * kPi);
        lobe.tone = 0.28f + 0.8f * std::max(0.0f, Dot(n, kLight));
        lobe.spec = std::pow(std::max(0.0f, Dot(n, kHalf)), 8.0f);
        lobes.push_back(std::move(lobe));
    }
    std::stable_sort(lobes.begin(), lobes.end(), [](const Lobe& a, const Lobe& b) { return a.depth < b.depth; });
    ComPtr<ID2D1Geometry> outline = cv.Union(all);
    if (!outline) return nullptr;

    cv.Shadow(outline.Get(), 0.07f);
    cv.Fill(outline.Get(), Tone(rind, 0.15f), 1.0f);
    for (const Lobe& lb : lobes) {
        const float w = std::max(0.5f * (lb.box.right - lb.box.left), 0.5f);
        const float h = std::max(0.5f * (lb.box.bottom - lb.box.top), 0.5f);
        const D2D1_POINT_2F mid = {0.5f * (lb.box.left + lb.box.right), 0.5f * (lb.box.top + lb.box.bottom)};
        auto body = cv.Radial(mid, {-0.32f * w, -0.3f * h}, 1.25f * w + 1.0f, 1.02f * h + 1.0f,
                              {Stop(0.0f, Tone(rind, lb.tone + 0.3f)), Stop(0.55f, Tone(rind, lb.tone)),
                               Stop(1.0f, Tone(rind, lb.tone - 0.3f))});
        for (size_t i = 0; i < lb.pieces.size(); ++i) {
            ID2D1PathGeometry* piece = lb.pieces[i].Get();
            if (!piece) continue;
            cv.Fill(piece, body.Get());
            cv.Clip(piece);
            // Shade deepening into the grooves either side.
            cv.Stroke(piece, Tone(rind, lb.tone - 0.55f), 0.13f, 1.1f * w);
            cv.Stroke(piece, Tone(rind, lb.tone - 0.6f), 0.18f, 0.45f * w);
            if (striped) {
                cv.Stroke(piece, kHeirloomStripe, 0.45f, 0.8f * w);
                cv.Stroke(piece, Mix(kHeirloomStripe, kShadow, 0.3f), 0.4f, 0.3f * w);
            }
            // Fine pale striations down the rind.
            if (i == lb.main)
                for (float f : {-0.3f, -0.1f, 0.14f, 0.32f}) {
                    Pts fibre;
                    const float th = lb.th + f * step;
                    for (int k = 0; k <= 12; ++k) {
                        const float p = kPi * (0.12f + 0.7f * static_cast<float>(k) / 12.0f);
                        if (g.Normal(th, p).z < 0.1f) continue;
                        const Vec3 v = g.At(th, p);
                        fibre.push_back({c.x + v.x, c.y - v.y});
                    }
                    cv.Lines(fibre, Tone(rind, lb.tone + 0.5f), 0.12f, 0.6f * cv.hair);
                }
            // A waxy sheen down the lobe, where it faces the light.
            if (i == lb.main && lb.spec > 0.05f) {
                Pts streak;
                const float th = lb.th - 0.16f * step;
                for (int k = 0; k <= 10; ++k) {
                    const float p = kPi * (0.26f + 0.36f * static_cast<float>(k) / 10.0f);
                    if (g.Normal(th, p).z < 0.25f) continue;
                    const Vec3 v = g.At(th, p);
                    streak.push_back({c.x + v.x, c.y - v.y});
                }
                if (auto line = cv.Path(streak, false)) {
                    const float a = std::min(1.0f, 1.3f * lb.spec);
                    cv.Stroke(line.Get(), kWhite, 0.07f * a, 0.7f * w);
                    cv.Stroke(line.Get(), kWhite, 0.09f * a, 0.38f * w);
                    cv.Stroke(line.Get(), kWhite, 0.14f * a, std::max(0.16f * w, 0.6f * cv.hair));
                }
            }
            cv.Unclip();
            // The groove.
            cv.Stroke(piece, Tone(rind, lb.tone - 0.62f), 0.5f, 0.85f * cv.hair);
        }
    }
    // Over the whole: the underside in shade, a soft light on the upper left,
    // and a bright rim along the lit edge.
    cv.Clip(outline.Get());
    const D2D1_RECT_F area = {c.x - 1.5f * g.R, c.y - 2.0f * g.B, c.x + 1.5f * g.R, c.y + 2.0f * g.B};
    if (auto br = cv.Linear({c.x, c.y - 0.15f * g.B}, {c.x, c.y + g.lift},
                            {Stop(0.0f, kUnderside, 0.0f), Stop(0.6f, kUnderside, 0.22f), Stop(1.0f, kUnderside, 0.5f)}))
        cv.Rect(area, br.Get());
    if (auto br = cv.Radial({c.x - 0.42f * g.R, c.y - 0.5f * g.B}, {0, 0}, 0.85f * g.R, 0.7f * g.B,
                            {Stop(0.0f, kWhite, 0.2f), Stop(1.0f, kWhite, 0.0f)}))
        cv.Rect(area, br.Get());
    if (auto br = cv.Linear({c.x - g.R, c.y - g.B}, {c.x + 0.1f * g.R, c.y + 0.2f * g.B},
                            {Stop(0.0f, kWhite, 0.45f), Stop(1.0f, kWhite, 0.0f)}))
        cv.Stroke(outline.Get(), br.Get(), 2.4f * cv.hair);
    cv.Unclip();
    cv.Stroke(outline.Get(), Mix(rind.dark, kShadow, 0.5f), 0.6f, cv.hair);
    return outline;
}

// A twisted stalk standing from `foot` (cell px): it leans and curves more
// toward its cut top, is ridged with a twist, and flares where it grips.
void PaintStalk(const Canvas& cv, D2D1_POINT_2F foot, float len, float lean, float w, const Shade3& col) {
    constexpr int n = 14;
    Pts spine;
    std::vector<float> ang, wid;
    D2D1_POINT_2F p = foot;
    for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float a = lean * (0.35f + 1.1f * t * t);
        spine.push_back(p);
        ang.push_back(a);
        const float flare = Saturate(1.0f - t / 0.22f);
        wid.push_back(w * (1.0f - 0.4f * t) * (1.0f + 0.55f * flare * flare));
        p = {p.x + std::sin(a) * len / n, p.y - std::cos(a) * len / n};
    }
    // The hollow it stands in.
    cv.Oval(Add(foot, 0.1f * w, 0.1f * w), 0.95f * w, 0.36f * w, kUnderside, 0.4f);
    auto geo = cv.Path(Tube(spine, wid), true);
    if (!geo) return;
    const D2D1_POINT_2F m = spine[n / 2];
    const float ca = std::cos(ang[n / 2]), sa = std::sin(ang[n / 2]), hw = 0.6f * wid[n / 2];
    if (auto br = cv.Linear({m.x - ca * hw, m.y - sa * hw}, {m.x + ca * hw, m.y + sa * hw},
                            {Stop(0.0f, col.lit), Stop(0.45f, col.mid), Stop(1.0f, col.dark)}))
        cv.Fill(geo.Get(), br.Get());
    cv.Clip(geo.Get());
    // Ridges twisting up it (round its far side, out of sight).
    for (int k = 0; k < 5; ++k) {
        Pts ridge;
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            const float f = -1.2f + 0.5f * static_cast<float>(k) + 0.45f * t;
            const float o = 0.5f * wid[static_cast<size_t>(i)] * f;
            ridge.push_back({spine[static_cast<size_t>(i)].x + std::cos(ang[static_cast<size_t>(i)]) * o,
                             spine[static_cast<size_t>(i)].y + std::sin(ang[static_cast<size_t>(i)]) * o});
        }
        cv.Lines(ridge, col.dark, 0.45f, std::max(0.7f * cv.hair, 0.09f * w));
    }
    cv.Unclip();
    cv.Stroke(geo.Get(), Mix(col.dark, kShadow, 0.4f), 0.65f, 0.85f * cv.hair);
    // The cut top, and the woody flare where it grips the pumpkin.
    const D2D1_POINT_2F tip = spine[n];
    const float tw = 0.5f * wid[n];
    if (auto cap = cv.Path(Ellipse(tip, tw, 0.38f * tw, 16, ang[n]), true)) {
        cv.Fill(cap.Get(), Mix(col.lit, kWhite, 0.3f), 1.0f);
        cv.Stroke(cap.Get(), col.dark, 0.6f, 0.6f * cv.hair);
    }
    for (int k = -2; k <= 2; ++k) {
        const float x = static_cast<float>(k) * 0.24f * w;
        cv.Line(Add(foot, x, -0.12f * w), Add(foot, 1.9f * x, 0.16f * w), col.dark, 0.45f, std::max(0.6f * cv.hair, 0.1f * w));
    }
}

// A curl of vine from its root (cell px 0, 0), reaching up and out to `side`
// and curling over.
void PaintTendril(const Canvas& cv, float len, int side, Rgb col) {
    constexpr int n = 48;
    Pts pts;
    D2D1_POINT_2F p = {0.0f, 0.0f};
    float h = -0.5f * kPi + static_cast<float>(side) * 0.75f;
    const float ds = len / static_cast<float>(n);
    for (int i = 0; i <= n; ++i) {
        pts.push_back(p);
        const float t = static_cast<float>(i) / static_cast<float>(n);
        h += static_cast<float>(side) * (0.8f + 16.0f * t * t) / len * ds;
        p = {p.x + std::cos(h) * ds, p.y + std::sin(h) * ds};
    }
    const float w0 = std::max(0.9f * cv.hair, 0.035f * len), w1 = std::max(0.55f * cv.hair, 0.015f * len);
    for (int i = 0; i < n; ++i)
        cv.Line(pts[static_cast<size_t>(i)], pts[static_cast<size_t>(i) + 1], col, 1.0f,
                Lerp(w0, w1, static_cast<float>(i) / static_cast<float>(n)));
    Pts lit(pts.begin(), pts.begin() + n / 2);
    for (D2D1_POINT_2F& q : lit) q = Add(q, -0.2f * w0, -0.25f * w0);
    cv.Lines(lit, Mix(col, kWhite, 0.4f), 0.5f, std::max(0.45f * cv.hair, 0.4f * w0));
}

// A big pumpkin leaf going over, on its stem: the stem's foot at the cell's
// anchor, standing up; the blade seen from above and in front, its lobes
// yellowing at the edges and browning in spots.
void PaintFrond(const Canvas& cv, const Scenery::Frond& f, Rng& rng) {
    const float size = f.size, stem = f.stem;
    const float bend = (f.lean >= 0.0f ? 1.0f : -1.0f) * 0.1f * stem;
    // The stem, hollow and ribbed, buried a little at its foot.
    Pts spine;
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        spine.push_back({bend * std::sin(kPi * t) * (1.0f - 0.3f * t), 4.0f * cv.s - (stem + 4.0f * cv.s) * t});
    }
    const float sw = std::max(1.4f * cv.hair, 0.06f * size);
    cv.Lines(spine, kFrondDark, 1.0f, sw);
    {
        Pts lit = spine;
        for (D2D1_POINT_2F& q : lit) q = Add(q, -0.22f * sw, 0.0f);
        cv.Lines(lit, kFrondLit, 0.8f, std::max(0.6f * cv.hair, 0.35f * sw));
    }
    // The blade: broad and heart-shaped with three to five shallow lobes,
    // seen from above and in front as it tips back.
    const D2D1_POINT_2F a = spine.back();
    const float squash = 0.66f, turn = rng.Range(-0.2f, 0.2f);
    const float ct = std::cos(turn), st = std::sin(turn);
    auto place = [&](float x, float y) { return D2D1_POINT_2F{a.x + x * ct - y * st, a.y + x * st + y * ct}; };
    auto bump = [](float p, float c) {
        const float d = (p - c) / 0.55f;
        return std::exp(-d * d);
    };
    constexpr float kLobeAt[3] = {0.0f, 1.2f, 2.3f};
    const float lobes[3] = {1.0f, rng.Range(0.88f, 0.96f), rng.Range(0.7f, 0.82f)};
    Pts blade;
    constexpr int n = 120;
    for (int i = 0; i < n; ++i) {
        const float p = -kPi + kTau * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const float l = std::max({lobes[0] * bump(p, kLobeAt[0]), lobes[1] * std::max(bump(p, -kLobeAt[1]), bump(p, kLobeAt[1])),
                                  lobes[2] * std::max(bump(p, -kLobeAt[2]), bump(p, kLobeAt[2]))});
        const float notch = (std::fabs(p) - kPi) / 0.32f;  // where the stem meets it
        const float r = 0.5f * size * (0.6f + 0.4f * l) * (1.0f - 0.55f * std::exp(-notch * notch)) *
                        (1.0f + 0.025f * std::sin(29.0f * p));
        blade.push_back(place(r * std::sin(p), -r * std::cos(p) * squash));
    }
    auto geo = cv.Path(blade, true);
    if (!geo) return;
    const Rgb edge = Mix(kFrondMid, kFrondTurn, 0.3f + 0.6f * f.turned);
    cv.Shadow(geo.Get(), 0.05f);
    if (auto br = cv.Radial(place(0.0f, -0.15f * size * squash), {-0.1f * size, -0.06f * size}, 0.6f * size, 0.45f * size,
                            {Stop(0.0f, Mix(kFrondLit, kFrondTurn, 0.25f * f.turned)), Stop(0.55f, Mix(kFrondMid, edge, 0.3f)),
                             Stop(1.0f, edge)}))
        cv.Fill(geo.Get(), br.Get());
    cv.Clip(geo.Get());
    // Brown, dry patches at the edge.
    for (int k = 0; k < 4; ++k) {
        const float p = rng.Range(-2.6f, 2.6f), r = 0.5f * size * rng.Range(0.8f, 1.0f);
        const D2D1_POINT_2F at = place(r * std::sin(p), -r * std::cos(p) * squash);
        cv.Oval(at, rng.Range(0.1f, 0.18f) * size, rng.Range(0.06f, 0.1f) * size, kFrondBrown, 0.3f + 0.4f * f.turned);
    }
    // Veins from the foot to each lobe, and branching.
    for (float p : {0.0f, -kLobeAt[1], kLobeAt[1], -kLobeAt[2], kLobeAt[2]}) {
        const float r = 0.47f * size * (std::fabs(p) > 2.0f ? lobes[2] : (p != 0.0f ? lobes[1] : lobes[0]));
        Pts vein;
        for (int i = 0; i <= 6; ++i) {
            const float t = static_cast<float>(i) / 6.0f;
            const float pp = p + 0.12f * t * t * (p >= 0.0f ? 1.0f : -1.0f);
            vein.push_back(place(r * t * std::sin(pp), -r * t * std::cos(pp) * squash));
        }
        cv.Lines(vein, kFrondVein, 0.55f, std::max(0.7f * cv.hair, 0.025f * size));
        for (float t : {0.4f, 0.7f})
            for (float sgn : {-1.0f, 1.0f}) {
                const float pp = p + sgn * 0.45f;
                cv.Line(place(r * t * std::sin(p), -r * t * std::cos(p) * squash),
                        place(r * (t + 0.25f) * std::sin(pp), -r * (t + 0.25f) * std::cos(pp) * squash), kFrondVein, 0.35f,
                        std::max(0.5f * cv.hair, 0.014f * size));
            }
    }
    // In its own shade low down, where it tips away.
    if (auto br = cv.Linear(place(0.0f, -0.4f * size * squash), place(0.0f, 0.25f * size * squash),
                            {Stop(0.0f, kFrondDark, 0.0f), Stop(1.0f, kFrondDark, 0.45f)}))
        cv.Rect({a.x - size, a.y - size, a.x + size, a.y + size}, br.Get());
    cv.Unclip();
    cv.Stroke(geo.Get(), Mix(kFrondDark, kFrondBrown, 0.4f * f.turned), 0.7f, 0.85f * cv.hair);
}

// ---------------------------------------------------------------------------
// Carved faces
// ---------------------------------------------------------------------------

Pts Mirror(const Pts& p) {
    Pts m;
    for (auto it = p.rbegin(); it != p.rend(); ++it) m.push_back({-it->x, it->y});
    return m;
}

// A face's cuts in face units (u right, v down; see kFaceU).
std::vector<Cut> FaceCuts(int face) {
    std::vector<Cut> cuts;
    auto oval = [](float u, float v, float ru, float rv) { return Ellipse({u, v}, ru, rv, 28); };
    switch (face) {
    case 0: {  // classic: triangle eyes and nose, a grin with three teeth
        const Pts eye = {{-0.64f, -0.1f}, {-0.16f, -0.1f}, {-0.4f, -0.62f}};
        cuts.push_back({eye});
        cuts.push_back({Mirror(eye)});
        cuts.push_back({{{-0.12f, 0.15f}, {0.12f, 0.15f}, {0.0f, -0.05f}}});
        cuts.push_back({{{-0.8f, 0.22f}, {-0.52f, 0.35f}, {-0.3f, 0.4f}, {-0.3f, 0.53f}, {-0.14f, 0.53f}, {-0.14f, 0.42f},
                         {0.14f, 0.42f}, {0.14f, 0.53f}, {0.3f, 0.53f}, {0.3f, 0.4f}, {0.52f, 0.35f}, {0.8f, 0.22f},
                         {0.62f, 0.52f}, {0.36f, 0.69f}, {0.1f, 0.76f}, {0.1f, 0.63f}, {-0.08f, 0.63f}, {-0.08f, 0.77f},
                         {-0.36f, 0.69f}, {-0.62f, 0.52f}}});
        break;
    }
    case 1: {  // jagged: slanted eyes, a grin full of sharp teeth
        const Pts eye = {{-0.7f, -0.5f}, {-0.14f, -0.22f}, {-0.24f, -0.05f}, {-0.63f, -0.19f}};
        cuts.push_back({eye});
        cuts.push_back({Mirror(eye)});
        cuts.push_back({{{-0.1f, 0.06f}, {0.1f, 0.06f}, {0.0f, 0.21f}}});
        Pts mouth;
        constexpr int n = 9;
        auto w = [](float u) { return 1.0f - (u / 0.84f) * (u / 0.84f); };
        for (int i = 0; i <= 2 * n; ++i) {  // the upper lip, fangs hanging
            const float u = -0.84f + 1.68f * static_cast<float>(i) / (2.0f * n);
            mouth.push_back({u, 0.3f + 0.08f * w(u) + (i % 2 ? 0.17f * w(u) : 0.0f)});
        }
        for (int i = 2 * n - 1; i >= 1; --i) {  // the lower lip, fangs standing between them
            const float u = -0.84f + 1.68f * static_cast<float>(i) / (2.0f * n);
            mouth.push_back({u, 0.3f + 0.42f * w(u) - (i % 2 ? 0.0f : 0.17f * w(u))});
        }
        cuts.push_back({mouth});
        break;
    }
    case 2:  // surprised: round eyes, a round mouth
        cuts.push_back({oval(-0.38f, -0.3f, 0.19f, 0.25f)});
        cuts.push_back({oval(0.38f, -0.3f, 0.19f, 0.25f)});
        cuts.push_back({oval(0.0f, 0.46f, 0.15f, 0.22f)});
        break;
    default: {  // cute: tall eyes with a glint of rind left in each, a small smile
        for (float sx : {-1.0f, 1.0f})
            cuts.push_back({oval(sx * 0.34f, -0.26f, 0.15f, 0.25f), oval(sx * 0.34f - 0.045f, -0.35f, 0.05f, 0.065f)});
        Pts smile;
        for (int i = 0; i <= 16; ++i) {
            const float u = -0.38f + 0.76f * static_cast<float>(i) / 16.0f;
            smile.push_back({u, 0.27f + 0.3f * (1.0f - (u / 0.38f) * (u / 0.38f))});
        }
        for (int i = 16; i >= 0; --i) {
            const float u = -0.29f + 0.58f * static_cast<float>(i) / 16.0f;
            smile.push_back({u, 0.31f + 0.12f * (1.0f - (u / 0.29f) * (u / 0.29f))});
        }
        cuts.push_back({smile});
        break;
    }
    }
    return cuts;
}

// A face's cuts on a gourd about `c` (cell px), looking `turn` to the side:
// edges divided finely, so they follow the rind round.
std::vector<Cut> PlaceCuts(const Gourd& g, int face, float turn, D2D1_POINT_2F c) {
    std::vector<Cut> cuts = FaceCuts(face);
    for (Cut& cut : cuts)
        for (Pts& fig : cut) {
            Pts fine;
            for (size_t i = 0; i < fig.size(); ++i) {
                const D2D1_POINT_2F a = fig[i], b = fig[(i + 1) % fig.size()];
                const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / 0.04f)));
                for (int k = 0; k < n; ++k) fine.push_back(LerpPt(a, b, static_cast<float>(k) / static_cast<float>(n)));
            }
            for (D2D1_POINT_2F& p : fine) {
                const Vec3 v = g.Face(turn, p.x, p.y);
                p = {c.x + v.x, c.y - v.y};
            }
            fig = std::move(fine);
        }
    return cuts;
}

// The lid's zigzag cut round the stalk: the stretches of it that show.
std::vector<Pts> LidSeam(const Gourd& g, D2D1_POINT_2F c, float turn) {
    std::vector<Pts> runs;
    Pts run;
    constexpr int n = 56;
    for (int i = 0; i <= n; ++i) {
        const float th = turn + kTau * static_cast<float>(i) / static_cast<float>(n);
        const float ps = 0.44f + (i % 2 ? 0.035f : -0.035f);
        if (g.Normal(th, ps).z > 0.1f) {
            const Vec3 v = g.At(th, ps);
            run.push_back({c.x + v.x, c.y - v.y});
        } else if (!run.empty()) {
            if (run.size() > 1) runs.push_back(run);
            run.clear();
        }
    }
    if (run.size() > 1) runs.push_back(run);
    return runs;
}

// The cuts as they look on the rind: dark (the lantern's light is a sprite
// of its own, drawn over them), each edge in shadow.
void PaintCuts(const Canvas& cv, const std::vector<Cut>& cuts, const std::vector<Pts>& lid) {
    for (const Cut& cut : cuts) {
        auto geo = cv.Figures(cut, true, true);
        cv.Fill(geo.Get(), kUnlit, 1.0f);
        cv.Stroke(geo.Get(), kUnderside, 0.35f, 2.6f * cv.hair);  // the rind darkens toward the cut
        cv.Stroke(geo.Get(), kShadow, 0.6f, 1.2f * cv.hair);
    }
    for (const Pts& seam : lid) cv.Lines(seam, kShadow, 0.3f, 0.7f * cv.hair);
}

// What shows through the cuts while the candle burns, at full light (the
// sprite's tint dims it with the flame): the lit inner wall, brightest low in
// the middle where the flame is, and the cut rind showing along each cut's
// lower edge. The lid's seam glows a little too.
void PaintLantern(const Canvas& cv, const std::vector<Cut>& cuts, const std::vector<Pts>& lid, D2D1_POINT_2F flame, float R,
                  float turn) {
    const float band = std::max(0.9f * cv.hair, 0.075f * R);
    auto inner = cv.Radial(flame, {0, 0}, 0.9f * R, 0.85f * R,
                           {Stop(0.0f, kFlameCore), Stop(0.35f, Mix(kFlameCore, kFlameInner, 0.55f)), Stop(0.75f, kFlameInner),
                            Stop(1.0f, kFlameDeep)});
    for (const Cut& cut : cuts) {
        auto geo = cv.Figures(cut, true, true);
        if (!geo) continue;
        cv.Fill(geo.Get(), kFlesh, 1.0f);
        cv.Clip(geo.Get());
        cv.Shifted(geo.Get(), -1.6f * turn * band, -band, inner.Get());
        cv.Unclip();
        cv.Stroke(geo.Get(), Mix(kFlameDeep, kUnlit, 0.5f), 0.7f, 0.9f * cv.hair);
    }
    for (const Pts& seam : lid) cv.Lines(seam, kFlameInner, 0.4f, 0.6f * cv.hair);
}

// Running stitches just inside a patch's edge.
void Stitches(const Canvas& cv, const Pts& patch, D2D1_POINT_2F centre, Rgb thread, float width, int per) {
    for (size_t i = 0; i < patch.size(); ++i) {
        const D2D1_POINT_2F a = LerpPt(patch[i], centre, 0.17f), b = LerpPt(patch[(i + 1) % patch.size()], centre, 0.17f);
        for (int k = 0; k < per; ++k) {
            const float t0 = (static_cast<float>(k) + 0.2f) / static_cast<float>(per);
            const float t1 = (static_cast<float>(k) + 0.7f) / static_cast<float>(per);
            cv.Line(LerpPt(a, b, t0), LerpPt(a, b, t1), thread, 0.85f, width);
        }
    }
}

// ---------------------------------------------------------------------------
// Scarecrows
// ---------------------------------------------------------------------------

// A plaid over `area` (cell px): broad bands both ways, darker where they
// cross, and fine light lines between.
void PaintPlaid(const Canvas& cv, const D2D1_RECT_F& area, const Plaid& p, float period) {
    cv.Rect(area, p.base, 1.0f);
    const float band = 0.38f * period, fine = std::max(0.6f * cv.hair, 0.07f * period);
    for (float x = area.left - period; x < area.right; x += period) {
        cv.Rect({x, area.top, x + band, area.bottom}, p.band, 0.42f);
        cv.Rect({x + 0.68f * period, area.top, x + 0.68f * period + fine, area.bottom}, p.line, 0.5f);
    }
    for (float y = area.top - period; y < area.bottom; y += period) {
        cv.Rect({area.left, y, area.right, y + band}, p.band, 0.42f);
        cv.Rect({area.left, y + 0.68f * period, area.right, y + 0.68f * period + fine}, p.line, 0.45f);
    }
}

// A tuft of straw from `root` toward `angle`, fanning `spread` either side.
void PaintStraw(const Canvas& cv, D2D1_POINT_2F root, float angle, float len, float spread, int n, float width, Rng& rng) {
    for (int i = 0; i < n; ++i) {
        const float a = angle + spread * (2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n) - 1.0f) +
                        rng.Range(-0.1f, 0.1f);
        const float l = len * rng.Range(0.6f, 1.1f), bow = rng.Range(-0.1f, 0.1f) * l;
        const D2D1_POINT_2F mid = {root.x + std::cos(a) * 0.5f * l - std::sin(a) * bow,
                                   root.y + std::sin(a) * 0.5f * l + std::cos(a) * bow};
        const D2D1_POINT_2F tip = {root.x + std::cos(a) * l, root.y + std::sin(a) * l};
        const float pick = rng.Float();
        const Rgb col = pick < 0.35f ? kStraw.lit : (pick < 0.8f ? kStraw.mid : kStraw.dark);
        cv.Line(root, mid, col, 1.0f, width);
        cv.Line(mid, tip, col, 1.0f, 0.7f * width);
    }
}

// The kerchief round the neck and its knot, on the side it faces.
void PaintKerchief(const Canvas& cv, const Scenery::Scarecrow& m) {
    const float S = m.S;
    const Kerchief& k = kKerchiefs[m.kerchief];
    const float y = -(m.knotY + 0.012f * S), half = 0.105f * S, thick = 0.042f * S;
    Pts band;
    for (int i = 0; i <= 12; ++i) {
        const float u = -1.0f + 2.0f * static_cast<float>(i) / 12.0f;
        band.push_back({u * half, y - 0.5f * thick + 0.02f * S * u * u});
    }
    for (int i = 12; i >= 0; --i) {
        const float u = -1.0f + 2.0f * static_cast<float>(i) / 12.0f;
        band.push_back({u * half * 1.04f, y + 0.5f * thick + 0.028f * S * u * u + 0.004f * S * std::sin(9.0f * u)});
    }
    if (auto g = cv.Path(band, true)) {
        if (auto br = cv.Linear({0, y - thick}, {0, y + thick},
                                {Stop(0.0f, Mix(k.main, kWhite, 0.15f)), Stop(1.0f, Mix(k.main, kShadow, 0.35f))}))
            cv.Fill(g.Get(), br.Get());
        cv.Clip(g.Get());
        if (k.dotted) {
            for (int i = -4; i <= 4; ++i)
                cv.Dot({static_cast<float>(i) * 0.026f * S, y + (i % 2 ? 0.006f : -0.006f) * S},
                       std::max(0.5f * cv.hair, 0.0065f * S), k.accent, 0.85f);
        } else {
            for (float u : {-0.6f, -0.1f, 0.4f})
                cv.Rect({u * half, y - thick, u * half + 0.022f * S, y + thick}, k.accent, 0.85f);
        }
        if (auto br = cv.Linear({-half, 0}, {half, 0}, {Stop(0.0f, kShadow, 0.0f), Stop(1.0f, kShadow, 0.3f)}))
            cv.Rect({-half * 1.2f, y - thick, half * 1.2f, y + thick}, br.Get());
        cv.Unclip();
        cv.Stroke(g.Get(), Mix(k.main, kShadow, 0.55f), 0.6f, 0.8f * cv.hair);
    }
    const D2D1_POINT_2F knot = {m.knotX, -m.knotY};
    if (auto br = cv.Radial(Add(knot, -0.008f * S, -0.008f * S), {0, 0}, 0.034f * S, 0.03f * S,
                            {Stop(0.0f, Mix(k.main, kWhite, 0.2f)), Stop(1.0f, Mix(k.main, kShadow, 0.35f))})) {
        cv.dc->FillEllipse(D2D1::Ellipse(knot, 0.03f * S, 0.026f * S), br.Get());
        cv.brush->SetColor(Color(Mix(k.main, kShadow, 0.55f), 0.6f));
        cv.dc->DrawEllipse(D2D1::Ellipse(knot, 0.03f * S, 0.026f * S), cv.brush, 0.7f * cv.hair);
    }
}

// The scarecrow on its post, anchored at the post's foot: the crossbar, the
// stuffed shirt with its sleeves along the bar, a rope belt, straw poking out
// at the neck and the hem, and the kerchief round its neck.
void PaintScarecrow(const Canvas& cv, const Scenery::Scarecrow& m, Rng& rng) {
    const float S = m.S;
    const Plaid& plaid = kPlaids[m.shirt];
    const float period = std::max(3.0f * cv.hair, 0.062f * S);
    // Post and crossbar.
    const float pw = 0.05f * S, top = -(m.armY + 0.03f * S);
    const D2D1_RECT_F post = {-0.5f * pw, top, 0.5f * pw, 0.06f * S};
    if (auto br = cv.Shade({-0.5f * pw, 0}, {0.5f * pw, 0}, kWood, 0.45f))
        cv.Rect(post, br.Get());
    for (float f : {-0.22f, 0.12f, 0.3f}) {
        Pts grain;
        for (int i = 0; i <= 8; ++i) {
            const float y = Lerp(top, 0.06f * S, static_cast<float>(i) / 8.0f);
            grain.push_back({f * pw + 0.04f * pw * std::sin(0.4f * y / std::max(cv.s, 0.1f) + 7.0f * f), y});
        }
        cv.Lines(grain, kWood.dark, 0.35f, 0.6f * cv.hair);
    }
    cv.Oval({0.08f * pw, -0.2f * S}, 0.16f * pw, 0.28f * pw, kWood.dark, 0.55f);
    const float bar = 0.034f * S;
    const D2D1_RECT_F crossbar = {-0.47f * S, -m.armY - 0.5f * bar, 0.47f * S, -m.armY + 0.5f * bar};
    if (auto br = cv.Shade({0, crossbar.top}, {0, crossbar.bottom}, kWood))
        cv.Rect(crossbar, br.Get());
    for (float sx : {-1.0f, 1.0f})
        cv.Oval({sx * 0.47f * S, -m.armY}, 0.18f * bar, 0.5f * bar, Mix(kWood.lit, kWhite, 0.2f), 1.0f);

    // Sleeves along the bar (a hanging forearm drops from the elbow).
    std::vector<ComPtr<ID2D1PathGeometry>> sleeves;
    for (int k = 0; k < 2; ++k) {
        const float sx = k ? 1.0f : -1.0f;
        const D2D1_POINT_2F shoulder = {sx * 0.1f * S, -m.armY - 0.006f * S}, elbow = {sx * 0.24f * S, -m.armY};
        const D2D1_POINT_2F cuff = {m.handX[k], -m.handY[k]};
        Pts spine = Smooth({shoulder, LerpPt(shoulder, elbow, 0.5f), elbow, LerpPt(elbow, cuff, 0.5f), cuff}, 4, false);
        std::vector<float> w;
        for (size_t i = 0; i < spine.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(spine.size() - 1);
            w.push_back(S * (0.108f - 0.024f * t) * (1.0f + 0.07f * std::sin(19.0f * t + sx)));  // bunched as it's stuffed
        }
        sleeves.push_back(cv.Path(Tube(spine, w), true));
    }
    // The shirt: rounded shoulders, stuffed out, drawn in at the belt, a ragged hem.
    const float y0 = -(m.armY + 0.05f * S), yb = -0.45f * S, y1 = -0.355f * S;
    Pts shirt = {{-0.065f * S, y0 - 0.004f * S}, {-0.135f * S, y0 + 0.006f * S}, {-0.165f * S, y0 + 0.035f * S},
                 {-0.176f * S, -0.575f * S},     {-0.15f * S, yb},           {-0.168f * S, y1 + 0.02f * S}};
    Pts hem;
    constexpr int teeth = 9;
    for (int i = 0; i <= teeth; ++i) {
        const float u = -0.168f + 0.336f * static_cast<float>(i) / teeth;
        hem.push_back({u * S, y1 + (i % 2 ? 0.022f : -0.004f) * S + rng.Range(-0.006f, 0.006f) * S});
    }
    Pts body = Smooth(shirt, 4, false);
    body.insert(body.end(), hem.begin() + 1, hem.end() - 1);
    Pts right = Mirror(Smooth(shirt, 4, false));
    body.insert(body.end(), right.begin(), right.end());
    auto torso = cv.Path(body, true);

    // Shadows, then back to front: sleeves, then the torso over their roots.
    for (const auto& g : sleeves) cv.Shadow(g.Get(), 0.05f);
    cv.Shadow(torso.Get(), 0.05f);
    // Straw spilling from under the hem, in front of the post (drawn first, the shirt overlaps its roots).
    for (int i = 0; i < 5; ++i)
        PaintStraw(cv, {(-0.11f + 0.055f * static_cast<float>(i)) * S, y1 - 0.01f * S}, 0.5f * kPi + rng.Range(-0.35f, 0.35f),
                   0.075f * S, 0.35f, 4, std::max(0.8f * cv.hair, 0.011f * S), rng);
    const D2D1_RECT_F everywhere = {-0.6f * S, -0.9f * S, 0.6f * S, 0.1f * S};
    for (const auto& g : sleeves) {
        if (!g) continue;
        cv.Clip(g.Get());
        PaintPlaid(cv, everywhere, plaid, period);
        if (auto br = cv.Linear({0, -m.armY - 0.05f * S}, {0, -m.armY + 0.05f * S},
                                {Stop(0.0f, kWhite, 0.14f), Stop(0.45f, kWhite, 0.0f), Stop(1.0f, kShadow, 0.38f)}))
            cv.Rect(everywhere, br.Get());
        cv.Unclip();
        cv.Stroke(g.Get(), Mix(plaid.band, kShadow, 0.5f), 0.65f, 0.85f * cv.hair);
    }
    if (torso) {
        cv.Clip(torso.Get());
        PaintPlaid(cv, everywhere, plaid, period);
        // A patch sewn on, with big stitches.
        const float side = m.dir > 0 ? -1.0f : 1.0f, ps = 0.075f * S;
        const D2D1_POINT_2F pc = {side * 0.07f * S, -0.52f * S};
        Pts patch = Ellipse(pc, 0.7f * ps, 0.7f * ps, 4, 0.785f + rng.Range(-0.12f, 0.12f));
        if (auto pg = cv.Path(patch, true)) {
            cv.Fill(pg.Get(), kPatches[m.cloth], 1.0f);
            cv.Stroke(pg.Get(), Mix(kPatches[m.cloth], kShadow, 0.45f), 0.6f, 0.8f * cv.hair);
            Stitches(cv, patch, pc, Hex(0xEADFC8), std::max(0.6f * cv.hair, 0.007f * S), 3);
        }
        // Stuffed round: lit on the upper left, shaded to the right and below.
        if (auto br = cv.Radial({-0.06f * S, -0.62f * S}, {0, 0}, 0.2f * S, 0.24f * S,
                                {Stop(0.0f, kWhite, 0.16f), Stop(1.0f, kWhite, 0.0f)}))
            cv.Rect(everywhere, br.Get());
        if (auto br = cv.Linear({-0.1f * S, 0}, {0.16f * S, 0}, {Stop(0.0f, kShadow, 0.0f), Stop(1.0f, kShadow, 0.32f)}))
            cv.Rect(everywhere, br.Get());
        // Creases gathered at the belt.
        const float crease = std::max(0.7f * cv.hair, 0.008f * S);
        for (float x : {-0.09f, -0.03f, 0.04f, 0.1f}) {
            cv.Line({x * S, yb - 0.005f * S}, {x * 1.25f * S, yb - 0.075f * S}, kShadow, 0.22f, crease);
            cv.Line({x * S, yb + 0.01f * S}, {x * 1.15f * S, y1}, kShadow, 0.18f, crease);
        }
        // The button placket.
        cv.Line({0.004f * S, y0 + 0.02f * S}, {0.004f * S, yb}, Mix(plaid.band, kShadow, 0.3f), 0.45f, 0.9f * crease);
        for (int i = 0; i < 2; ++i) {
            const D2D1_POINT_2F b = {0.016f * S, Lerp(y0 + 0.07f * S, yb - 0.025f * S, static_cast<float>(i))};
            cv.Dot(b, std::max(0.7f * cv.hair, 0.011f * S), Hex(0xE8DCC0), 1.0f);
            cv.Dot(b, std::max(0.35f * cv.hair, 0.005f * S), kShadow, 0.5f);
        }
        cv.Unclip();
        cv.Stroke(torso.Get(), Mix(plaid.band, kShadow, 0.5f), 0.7f, 0.9f * cv.hair);
    }
    // The rope belt, knotted, its ends hanging.
    {
        const float h = 0.024f * S;
        Pts rope = {{-0.156f * S, yb - 0.006f * S}, {0.0f, yb + 0.004f * S}, {0.156f * S, yb - 0.006f * S}};
        rope = Smooth(rope, 6, false);
        cv.Lines(rope, Mix(kTwine, kShadow, 0.35f), 1.0f, h);
        cv.Lines(rope, kTwine, 1.0f, 0.65f * h);
        for (int i = 0; i < 10; ++i) {
            const float x = (-0.145f + 0.032f * static_cast<float>(i)) * S;
            cv.Line({x - 0.006f * S, yb - 0.4f * h}, {x + 0.006f * S, yb + 0.5f * h}, Mix(kTwine, kShadow, 0.5f), 0.6f,
                    std::max(0.5f * cv.hair, 0.004f * S));
        }
        const float kx = (m.dir > 0 ? 0.06f : -0.06f) * S;
        cv.Dot({kx, yb}, 0.75f * h, kTwine, 1.0f);
        cv.Line({kx, yb}, {kx - 0.012f * S, yb + 0.07f * S}, kTwine, 1.0f, 0.5f * h);
        cv.Line({kx, yb}, {kx + 0.016f * S, yb + 0.055f * S}, kTwine, 1.0f, 0.5f * h);
    }
    // Straw at the neck, and the collar's points.
    for (float sx : {-1.0f, 1.0f})
        PaintStraw(cv, {sx * 0.03f * S, -m.knotY + 0.004f * S}, -0.5f * kPi + sx * 0.75f, 0.075f * S, 0.4f, 5,
                   std::max(0.8f * cv.hair, 0.011f * S), rng);
    for (float sx : {-1.0f, 1.0f}) {
        Pts collar = {{sx * 0.012f * S, y0 + 0.004f * S}, {sx * 0.095f * S, y0 + 0.002f * S}, {sx * 0.05f * S, y0 + 0.06f * S}};
        if (auto g = cv.Path(collar, true)) {
            cv.Fill(g.Get(), Mix(plaid.base, kWhite, 0.18f), 1.0f);
            cv.Stroke(g.Get(), Mix(plaid.band, kShadow, 0.5f), 0.6f, 0.8f * cv.hair);
        }
    }
    PaintKerchief(cv, m);
}

// A straw hand, from its root at the cuff (the cell's anchor) out along +x.
void PaintHand(const Canvas& cv, float S, Rng& rng) {
    PaintStraw(cv, {-0.01f * S, 0.0f}, 0.0f, 0.12f * S, 0.42f, 11, std::max(0.8f * cv.hair, 0.012f * S), rng);
    for (float x : {0.004f, 0.016f})
        cv.Line({x * S, -0.03f * S}, {x * S + 0.004f * S, 0.03f * S}, kTwine, 1.0f, std::max(0.8f * cv.hair, 0.01f * S));
}

// A burlap sack for a head, about the cell's anchor: lumpy, gathered at the
// neck with twine, with stitched eyes (or buttons) and a stitched smile.
void PaintSack(const Canvas& cv, const Scenery::Scarecrow& m, Rng& rng) {
    const float r = m.headR, S = m.S;
    const float neck = m.head.lift;  // the neck below the centre
    Pts sack;
    const float p1 = rng.Range(0.0f, kTau), p2 = rng.Range(0.0f, kTau);
    for (int i = 0; i < 64; ++i) {
        const float a = kTau * static_cast<float>(i) / 64.0f;
        float rr = r * (1.0f + 0.035f * std::sin(3.0f * a + p1) + 0.025f * std::sin(5.0f * a + p2));
        const float down = std::max(0.0f, std::sin(a));  // pinched in toward the gathered neck
        rr *= 1.0f - 0.38f * std::pow(down, 6.0f);
        sack.push_back({rr * std::cos(a), 0.93f * rr * std::sin(a) - 0.04f * r});
    }
    // The gathered neck below it.
    Pts gather = {{-0.36f * r, 0.62f * r}, {0.36f * r, 0.62f * r}, {0.24f * r, neck + 0.02f * S}, {-0.24f * r, neck + 0.02f * S}};
    auto sackGeo = cv.Path(sack, true), gatherGeo = cv.Path(gather, true);
    cv.Shadow(sackGeo.Get(), 0.06f);
    if (auto br = cv.Linear({-0.3f * r, 0.6f * r}, {0.3f * r, neck}, {Stop(0.0f, kBurlap.mid), Stop(1.0f, kBurlap.dark)}))
        cv.Fill(gatherGeo.Get(), br.Get());
    for (float x : {-0.18f, -0.06f, 0.06f, 0.18f})
        cv.Line({x * r, 0.66f * r}, {x * 0.7f * r, neck + 0.015f * S}, kBurlap.dark, 0.6f, 0.8f * cv.hair);
    if (!sackGeo) return;
    if (auto br = cv.Radial({-0.3f * r, -0.35f * r}, {0, 0}, 1.45f * r, 1.4f * r,
                            {Stop(0.0f, kBurlap.lit), Stop(0.55f, kBurlap.mid), Stop(1.0f, kBurlap.dark)}))
        cv.Fill(sackGeo.Get(), br.Get());
    cv.Clip(sackGeo.Get());
    // The weave.
    const float pitch = std::max(1.8f * cv.hair, 0.085f * r);
    for (float x = -r; x < r; x += pitch) cv.Line({x, -r}, {x + 0.04f * r, r}, kBurlap.dark, 0.13f, 0.6f * cv.hair);
    for (float y = -r; y < r; y += pitch) cv.Line({-r, y}, {r, y + 0.03f * r}, kBurlap.dark, 0.11f, 0.6f * cv.hair);
    // Folds drawn in toward the neck.
    for (float x : {-0.55f, -0.2f, 0.22f, 0.5f}) {
        Pts fold = {{x * r, 0.3f * r}, {x * 0.7f * r, 0.65f * r}, {x * 0.45f * r, 0.95f * r}};
        cv.Lines(Smooth(fold, 4, false), kBurlap.dark, 0.35f, 0.9f * cv.hair);
    }
    cv.Unclip();
    cv.Stroke(sackGeo.Get(), Mix(kBurlap.dark, kShadow, 0.4f), 0.65f, 0.9f * cv.hair);
    // The face, turned toward the pumpkins.
    const float turn = static_cast<float>(m.dir) * 0.42f;
    auto at = [&](float u, float v) {
        const Vec3 p = m.head.Face(turn, u, v);
        return D2D1_POINT_2F{p.x, -p.y};
    };
    const float thread = std::max(0.8f * cv.hair, 0.06f * r);
    for (float sx : {-1.0f, 1.0f}) {
        const D2D1_POINT_2F e = at(sx * 0.36f, -0.24f);
        // A touch of blush below each eye.
        cv.Oval(Add(at(sx * 0.5f, 0.12f), 0.0f, 0.0f), 0.17f * r, 0.11f * r, Hex(0xC8624A), 0.22f);
        if (m.buttons) {
            const float er = 0.15f * r;
            cv.Dot(e, er, Hex(0x3A2C22), 1.0f);
            cv.Dot(Add(e, -0.3f * er, -0.32f * er), 0.32f * er, kWhite, 0.35f);
            cv.Dot(Add(e, -0.28f * er, 0.12f * er), 0.15f * er, kBurlap.mid, 0.9f);
            cv.Dot(Add(e, 0.28f * er, 0.12f * er), 0.15f * er, kBurlap.mid, 0.9f);
        } else {
            const float er = 0.1f * r;
            cv.Line(Add(e, -er, -er), Add(e, er, er), kThread, 0.9f, thread);
            cv.Line(Add(e, -er, er), Add(e, er, -er), kThread, 0.9f, thread);
        }
    }
    // A little triangle of sacking for a nose.
    {
        Pts nose = {at(-0.1f, 0.12f), at(0.1f, 0.12f), at(0.0f, -0.05f)};
        if (auto g = cv.Path(nose, true)) {
            cv.Fill(g.Get(), Mix(kBurlap.dark, kPatches[1], 0.4f), 1.0f);
            cv.Stroke(g.Get(), kThread, 0.6f, 0.6f * cv.hair);
        }
    }
    // The stitched smile: a gentle curve of thread, a few stitches across it.
    Pts smile;
    for (int i = 0; i <= 12; ++i) {
        const float u = -0.42f + 0.84f * static_cast<float>(i) / 12.0f;
        smile.push_back(at(u, 0.3f + 0.24f * (1.0f - (u / 0.42f) * (u / 0.42f))));
    }
    cv.Lines(smile, kThread, 0.9f, 0.85f * thread);
    for (size_t i = 2; i + 2 < smile.size(); i += 2) {
        const D2D1_POINT_2F a = smile[i - 1], b = smile[i + 1], c = smile[i];
        const float dx = b.x - a.x, dy = b.y - a.y, l = std::max(std::hypot(dx, dy), 1e-3f);
        const float h = 0.07f * r;
        cv.Line({c.x + dy / l * h, c.y - dx / l * h}, {c.x - dy / l * h, c.y + dx / l * h}, kThread, 0.8f, 0.6f * thread);
    }
    // Twine round the neck.
    const float ty = 0.66f * r, end = std::max(0.6f * cv.hair, 0.01f * S);
    for (float o : {0.0f, 0.045f}) {
        const D2D1_POINT_2F a = {-0.36f * r, ty + o * S}, b = {0.36f * r, ty + o * S + 0.01f * S};
        cv.Line(a, b, Mix(kTwine, kShadow, 0.4f), 1.0f, std::max(cv.hair, 0.022f * S));
        cv.Line(a, b, kTwine, 1.0f, std::max(0.6f * cv.hair, 0.013f * S));
    }
    const D2D1_POINT_2F knot = {static_cast<float>(-m.dir) * 0.22f * r, ty + 0.025f * S};
    cv.Dot(knot, 0.022f * S, kTwine, 1.0f);
    cv.Line(knot, Add(knot, -0.02f * S, 0.055f * S), kTwine, 1.0f, end);
    cv.Line(knot, Add(knot, 0.015f * S, 0.05f * S), kTwine, 1.0f, end);
}

// The kerchief's loose ends, hanging from the knot (the cell's anchor) down +y:
// a bandana's two points, or a scarf's tattered tail.
void PaintTail(const Canvas& cv, const Scenery::Scarecrow& m, Rng& rng) {
    const float S = m.S;
    const Kerchief& k = kKerchiefs[m.kerchief];
    const float lean = 0.025f * S * static_cast<float>(m.dir);
    std::vector<Pts> parts;
    if (k.dotted) {
        parts.push_back(Smooth({{-0.026f * S, 0.0f}, {-0.02f * S, 0.08f * S}, {lean, 0.165f * S}, {0.02f * S, 0.07f * S},
                                {0.026f * S, 0.0f}},
                               4, false));
        parts.push_back(Smooth({{0.0f, 0.0f}, {0.032f * S + lean, 0.07f * S}, {0.05f * S + lean, 0.12f * S},
                                {0.034f * S, 0.04f * S}, {0.02f * S, 0.0f}},
                               4, false));
    } else {
        Pts tail;
        const float len = 0.2f * S, half = 0.028f * S;
        for (int i = 0; i <= 10; ++i) {
            const float u = static_cast<float>(i) / 10.0f;
            tail.push_back({-half + lean * u * u + 0.008f * S * std::sin(kPi * u), u * len});
        }
        for (int i = 0; i <= 6; ++i)  // the tattered end
            tail.push_back({-half + lean + 2.0f * half * static_cast<float>(i) / 6.0f,
                            len + ((i % 2 ? -0.022f : 0.0f) + rng.Range(-0.006f, 0.006f)) * S});
        for (int i = 10; i >= 0; --i) {
            const float u = static_cast<float>(i) / 10.0f;
            tail.push_back({half * (1.0f - 0.1f * u) + lean * u * u + 0.008f * S * std::sin(kPi * u), u * len});
        }
        parts.push_back(tail);
    }
    for (const Pts& part : parts) {
        auto g = cv.Path(part, true);
        if (!g) continue;
        cv.Shadow(g.Get(), 0.05f);
        if (auto br = cv.Linear({-0.03f * S, 0}, {0.04f * S, 0},
                                {Stop(0.0f, Mix(k.main, kWhite, 0.15f)), Stop(1.0f, Mix(k.main, kShadow, 0.32f))}))
            cv.Fill(g.Get(), br.Get());
        cv.Clip(g.Get());
        if (k.dotted) {
            for (int i = 0; i < 8; ++i) {
                const float col = static_cast<float>(i % 4), row = static_cast<float>(i / 4);
                cv.Dot({(-0.03f + 0.022f * col + 0.011f * row) * S, (0.03f + 0.04f * row + 0.02f * static_cast<float>(i % 2)) * S},
                       std::max(0.5f * cv.hair, 0.0065f * S), k.accent, 0.85f);
            }
        } else {
            for (float v : {0.35f, 0.62f}) cv.Rect({-0.1f * S, v * 0.2f * S, 0.1f * S, v * 0.2f * S + 0.024f * S}, k.accent, 0.9f);
            cv.Dot({0.006f * S + lean * 0.5f, 0.13f * S}, 0.007f * S, kShadow, 0.5f);  // worn through
        }
        cv.Unclip();
        cv.Stroke(g.Get(), Mix(k.main, kShadow, 0.55f), 0.6f, 0.8f * cv.hair);
    }
    if (!k.dotted)
        for (int i = 0; i < 5; ++i) {
            const float x = -0.024f * S + lean + 0.012f * S * static_cast<float>(i);
            cv.Line({x, 0.198f * S}, {x + 0.003f * S, 0.225f * S}, k.main, 0.9f, std::max(0.7f * cv.hair, 0.008f * S));
        }
}

// Hats, about the foot they sit on (the cell's anchor; y down), r the head's radius.

// A witch's hat: a wide, slightly floppy brim; a tall cone whose tip has
// flopped over; a band with a brass buckle.
void PaintWitchHat(const Canvas& cv, float r, const Felt& felt, int dir) {
    const float flop = static_cast<float>(-dir);  // the tip flops away from the face
    Pts brim;
    for (int i = 0; i < 48; ++i) {
        const float a = kTau * static_cast<float>(i) / 48.0f;
        const float wob = 1.0f + 0.035f * std::sin(3.0f * a + 1.0f) + 0.02f * std::sin(5.0f * a);
        brim.push_back({1.48f * r * wob * std::cos(a), 0.3f * r * wob * std::sin(a) + 0.03f * r + 0.06f * r * std::cos(a) * flop});
    }
    auto brimGeo = cv.Path(brim, true);
    cv.Shadow(brimGeo.Get(), 0.06f);
    if (auto br = cv.Shade({-1.4f * r, -0.2f * r}, {1.4f * r, 0.3f * r}, felt.felt))
        cv.Fill(brimGeo.Get(), br.Get());
    // The brim's front edge, a little thicker than a line.
    if (brimGeo) {
        cv.Clip(brimGeo.Get());
        cv.Oval({0.0f, -0.05f * r}, 1.38f * r, 0.24f * r, kShadow, 0.18f);
        cv.Unclip();
        cv.Stroke(brimGeo.Get(), Mix(felt.felt.dark, kShadow, 0.5f), 0.7f, 0.9f * cv.hair);
    }
    // The cone, rising from the brim and flopping over at the top.
    Pts spine = Smooth({{0.0f, 0.0f}, {0.04f * r * flop, -0.9f * r}, {-0.06f * r * flop, -1.75f * r},
                        {0.3f * r * flop, -2.32f * r}, {0.82f * r * flop, -2.18f * r}, {1.0f * r * flop, -1.98f * r}},
                       6, false);
    std::vector<float> w;
    for (size_t i = 0; i < spine.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(spine.size() - 1);
        w.push_back(2.0f * r * std::max(0.03f, 0.62f * std::pow(1.0f - t, 1.25f) + 0.02f * std::sin(9.0f * t)));
    }
    Pts cone = Tube(spine, w);
    // Its foot follows the brim round (the near side lower).
    for (D2D1_POINT_2F& p : cone)
        if (p.y > -0.05f * r) p.y = 0.08f * r * std::sqrt(std::max(0.0f, 1.0f - (p.x / (0.62f * r)) * (p.x / (0.62f * r))));
    auto coneGeo = cv.Path(cone, true);
    if (!coneGeo) return;
    if (auto br = cv.Shade({-0.6f * r, 0}, {0.65f * r, 0}, felt.felt, 0.4f))
        cv.Fill(coneGeo.Get(), br.Get());
    cv.Clip(coneGeo.Get());
    // Creases where it bends, and the flopped tip in its own shade.
    for (int k = 0; k < 3; ++k) {
        const float y = (-1.45f - 0.18f * static_cast<float>(k)) * r;
        Pts crease = {{-0.3f * r, y + 0.05f * r}, {0.0f, y - 0.06f * r * flop}, {0.3f * r, y + 0.03f * r}};
        cv.Lines(Smooth(crease, 4, false), Mix(felt.felt.dark, kShadow, 0.5f), 0.45f, 0.9f * cv.hair);
    }
    cv.Oval({0.75f * r * flop, -2.12f * r}, 0.5f * r, 0.3f * r, kShadow, 0.25f);
    cv.Line({-0.42f * r, -0.4f * r}, {-0.14f * r, -1.5f * r}, kWhite, 0.12f, 0.12f * r);
    // The band, and its buckle (a little toward the face).
    Pts band;
    for (int i = 0; i <= 12; ++i) {
        const float u = -1.0f + 2.0f * static_cast<float>(i) / 12.0f;
        band.push_back({u * 0.62f * r, 0.08f * r * std::sqrt(std::max(0.0f, 1.0f - u * u)) + 0.02f * r});
    }
    for (int i = 12; i >= 0; --i) {
        const float u = -1.0f + 2.0f * static_cast<float>(i) / 12.0f;
        band.push_back({u * 0.56f * r, -0.36f * r + 0.07f * r * std::sqrt(std::max(0.0f, 1.0f - u * u))});
    }
    if (auto bg = cv.Path(band, true)) {
        if (auto br = cv.Linear({-0.6f * r, 0}, {0.6f * r, 0},
                                {Stop(0.0f, Mix(felt.band, kWhite, 0.2f)), Stop(1.0f, Mix(felt.band, kShadow, 0.4f))}))
            cv.Fill(bg.Get(), br.Get());
        cv.Stroke(bg.Get(), Mix(felt.band, kShadow, 0.5f), 0.5f, 0.7f * cv.hair);
    }
    const D2D1_POINT_2F bc = {static_cast<float>(dir) * 0.18f * r, -0.14f * r};
    const float bs = 0.17f * r;
    const D2D1_RECT_F outer = {bc.x - bs, bc.y - bs, bc.x + bs, bc.y + bs};
    if (auto br = cv.Linear({outer.left, outer.top}, {outer.right, outer.bottom}, {Stop(0.0f, kBuckleLit), Stop(1.0f, kBuckleDark)})) {
        cv.Rect(outer, br.Get());
        cv.Rect({bc.x - 0.55f * bs, bc.y - 0.55f * bs, bc.x + 0.55f * bs, bc.y + 0.55f * bs}, Mix(felt.band, kShadow, 0.2f), 1.0f);
        cv.Line(Add(bc, -0.05f * bs, -0.6f * bs), Add(bc, -0.05f * bs, 0.6f * bs), kBuckleLit, 1.0f,
                std::max(0.6f * cv.hair, 0.22f * bs));  // the prong
    }
    cv.Unclip();
    cv.Stroke(coneGeo.Get(), Mix(felt.felt.dark, kShadow, 0.5f), 0.75f, 0.9f * cv.hair);
}

// A battered top hat: a floppy brim, a dented crown with a patch, a faded
// band with a leaf tucked in it.
void PaintTopHat(const Canvas& cv, float r, int dir, int cloth, Rng& rng) {
    const Felt& felt = kTopHat;
    Pts brim;
    for (int i = 0; i < 40; ++i) {
        const float a = kTau * static_cast<float>(i) / 40.0f;
        const float droop = 0.06f * r * std::max(0.0f, std::cos(a) * static_cast<float>(dir));
        brim.push_back({1.12f * r * std::cos(a), 0.26f * r * std::sin(a) + 0.02f * r + droop});
    }
    auto brimGeo = cv.Path(brim, true);
    cv.Shadow(brimGeo.Get(), 0.06f);
    if (auto br = cv.Shade({-1.1f * r, 0}, {1.1f * r, 0}, felt.felt))
        cv.Fill(brimGeo.Get(), br.Get());
    cv.Stroke(brimGeo.Get(), Mix(felt.felt.dark, kShadow, 0.5f), 0.7f, 0.9f * cv.hair);
    // The crown: a little flared, dented on one side, its top knocked askew.
    auto topY = [r](float x) { return -1.34f * r + 0.08f * x; };
    Pts crown = {{-0.66f * r, -0.02f * r}, {-0.62f * r, -0.6f * r}, {-0.55f * r, -0.8f * r}, {-0.66f * r, -1.05f * r},
                 {-0.7f * r, topY(-0.7f * r)}};
    Pts rightSide = {{0.7f * r, topY(0.7f * r)}, {0.68f * r, -0.8f * r}, {0.64f * r, -0.3f * r}, {0.66f * r, -0.02f * r}};
    Pts outline = Smooth(crown, 4, false);
    Pts rs = Smooth(rightSide, 4, false);
    outline.insert(outline.end(), rs.begin(), rs.end());
    for (int i = 1; i < 12; ++i) {  // the near side of its foot, round the brim
        const float u = 1.0f - 2.0f * static_cast<float>(i) / 12.0f;
        outline.push_back({u * 0.66f * r, 0.07f * r * std::sqrt(std::max(0.0f, 1.0f - u * u)) - 0.02f * r});
    }
    auto crownGeo = cv.Path(outline, true);
    if (!crownGeo) return;
    if (auto br = cv.Shade({-0.7f * r, 0}, {0.7f * r, 0}, felt.felt, 0.35f))
        cv.Fill(crownGeo.Get(), br.Get());
    cv.Clip(crownGeo.Get());
    cv.Rect({-0.45f * r, -1.6f * r, -0.32f * r, 0.0f}, kWhite, 0.08f);
    // The dent's shadow and a crease.
    cv.Oval({-0.5f * r, -0.82f * r}, 0.2f * r, 0.3f * r, kShadow, 0.3f);
    cv.Lines(Smooth({{-0.3f * r, -1.25f * r}, {-0.05f * r, -1.05f * r}, {0.1f * r, -1.2f * r}}, 4, false), kShadow, 0.35f,
             0.9f * cv.hair);
    // A patch, stitched on.
    const D2D1_POINT_2F pc = {static_cast<float>(-dir) * 0.28f * r, -0.72f * r};
    Pts patch = Ellipse(pc, 0.24f * r, 0.24f * r, 4, 0.785f + rng.Range(-0.2f, 0.2f));
    if (auto pg = cv.Path(patch, true)) {
        cv.Fill(pg.Get(), Mix(kPatches[cloth], felt.felt.mid, 0.35f), 1.0f);
        cv.Stroke(pg.Get(), kShadow, 0.4f, 0.7f * cv.hair);
        Stitches(cv, patch, pc, Hex(0xD8C8A8), std::max(0.5f * cv.hair, 0.035f * r), 2);
    }
    // The faded band.
    cv.Rect({-0.8f * r, -0.36f * r, 0.8f * r, -0.1f * r}, felt.band, 1.0f);
    cv.Rect({-0.8f * r, -0.36f * r, 0.8f * r, -0.33f * r}, kWhite, 0.2f);
    if (auto br = cv.Linear({-0.66f * r, 0}, {0.66f * r, 0}, {Stop(0.0f, kShadow, 0.0f), Stop(1.0f, kShadow, 0.4f)}))
        cv.Rect({-0.8f * r, -1.6f * r, 0.8f * r, 0.2f * r}, br.Get());
    cv.Unclip();
    cv.Stroke(crownGeo.Get(), Mix(felt.felt.dark, kShadow, 0.6f), 0.7f, 0.9f * cv.hair);
    // Its top, askew, catching the light.
    Pts lid = Ellipse({0.0f, topY(0.0f)}, 0.7f * r, 0.13f * r, 24, std::atan(0.08f));
    if (auto lg = cv.Path(lid, true)) {
        cv.Fill(lg.Get(), Mix(felt.felt.mid, felt.felt.lit, 0.5f), 1.0f);
        cv.Stroke(lg.Get(), Mix(felt.felt.dark, kShadow, 0.5f), 0.6f, 0.8f * cv.hair);
    }
    // A leaf tucked in the band.
    const D2D1_POINT_2F lf = {static_cast<float>(dir) * 0.42f * r, -0.3f * r};
    Pts leaf;
    for (int i = 0; i < 14; ++i) {
        const float a = kTau * static_cast<float>(i) / 14.0f;
        const float rr = 0.28f * r * (0.55f + 0.45f * std::fabs(std::cos(2.5f * a)));
        leaf.push_back({lf.x + rr * std::sin(a) * 0.9f, lf.y - 0.12f * r - rr * std::cos(a)});
    }
    if (auto lg = cv.Path(leaf, true)) {
        cv.Fill(lg.Get(), Hex(0xD9542A), 1.0f);
        cv.Stroke(lg.Get(), Hex(0x7A2A12), 0.6f, 0.7f * cv.hair);
        cv.Line(lf, {lf.x, lf.y - 0.38f * r}, Hex(0xF2A050), 0.7f, 0.6f * cv.hair);
    }
}

// A wide straw hat: a woven brim seen from a little above with straws
// sticking out of its edge, a round crown, a ribbon.
void PaintStrawHat(const Canvas& cv, float r, Rgb ribbon, int dir, Rng& rng) {
    const D2D1_POINT_2F mid = {0.0f, 0.02f * r};
    Pts brim = Ellipse(mid, 1.64f * r, 0.36f * r, 56);
    auto brimGeo = cv.Path(brim, true);
    if (!brimGeo) return;
    cv.Shadow(brimGeo.Get(), 0.06f);
    if (auto br = cv.Radial({-0.5f * r, -0.1f * r}, {0, 0}, 2.0f * r, 0.6f * r,
                            {Stop(0.0f, kStraw.lit), Stop(0.6f, kStraw.mid), Stop(1.0f, kStraw.dark)}))
        cv.Fill(brimGeo.Get(), br.Get());
    cv.Clip(brimGeo.Get());
    for (float k : {0.55f, 0.72f, 0.88f})  // the weave's rings
        cv.Stroke(cv.Path(Ellipse(mid, 1.64f * k * r, 0.36f * k * r, 48), true).Get(), kStraw.dark, 0.3f, 0.7f * cv.hair);
    for (int i = 0; i < 28; ++i) {
        const float a = kTau * static_cast<float>(i) / 28.0f;
        cv.Line({mid.x + 0.6f * r * std::cos(a), mid.y + 0.13f * r * std::sin(a)},
                {mid.x + 1.64f * r * std::cos(a), mid.y + 0.36f * r * std::sin(a)},
                kStraw.dark, 0.14f, 0.6f * cv.hair);
    }
    cv.Oval({0.0f, 0.1f * r}, 1.5f * r, 0.2f * r, kShadow, 0.12f);  // the near brim's underside
    cv.Unclip();
    cv.Stroke(brimGeo.Get(), Mix(kStraw.dark, kShadow, 0.3f), 0.7f, 0.9f * cv.hair);
    for (int i = 0; i < 16; ++i) {  // frayed straws at the edge
        const float a = kTau * (static_cast<float>(i) + rng.Range(0.0f, 0.6f)) / 16.0f;
        const D2D1_POINT_2F p = {mid.x + 1.62f * r * std::cos(a), mid.y + 0.355f * r * std::sin(a)};
        const float l = rng.Range(0.08f, 0.2f) * r;
        cv.Line(p, {p.x + std::cos(a) * l, p.y + std::sin(a) * l * 0.4f}, kStraw.mid, 1.0f, std::max(0.6f * cv.hair, 0.025f * r));
    }
    // The crown.
    Pts dome;
    for (int i = 0; i <= 24; ++i) {
        const float a = kPi * static_cast<float>(i) / 24.0f;
        dome.push_back({-0.7f * r * std::cos(a), -0.04f * r - 0.84f * r * std::sin(a)});
    }
    for (int i = 1; i < 10; ++i) {
        const float u = 1.0f - 2.0f * static_cast<float>(i) / 10.0f;
        dome.push_back({u * 0.7f * r, -0.04f * r + 0.1f * r * std::sqrt(std::max(0.0f, 1.0f - u * u))});
    }
    auto domeGeo = cv.Path(dome, true);
    if (!domeGeo) return;
    if (auto br = cv.Radial({-0.25f * r, -0.6f * r}, {0, 0}, 1.0f * r, 0.95f * r,
                            {Stop(0.0f, kStraw.lit), Stop(0.65f, kStraw.mid), Stop(1.0f, kStraw.dark)}))
        cv.Fill(domeGeo.Get(), br.Get());
    cv.Clip(domeGeo.Get());
    for (float y = -0.84f * r; y < 0.1f * r; y += std::max(1.6f * cv.hair, 0.12f * r))
        cv.Line({-0.8f * r, y}, {0.8f * r, y + 0.02f * r}, kStraw.dark, 0.22f, 0.6f * cv.hair);
    // The ribbon, and its bow on the far side.
    cv.Rect({-0.8f * r, -0.26f * r, 0.8f * r, -0.04f * r}, ribbon, 1.0f);
    cv.Rect({-0.8f * r, -0.26f * r, 0.8f * r, -0.23f * r}, kWhite, 0.2f);
    if (auto br = cv.Linear({-0.7f * r, 0}, {0.7f * r, 0}, {Stop(0.0f, kShadow, 0.0f), Stop(1.0f, kShadow, 0.35f)}))
        cv.Rect({-0.8f * r, -1.0f * r, 0.8f * r, 0.2f * r}, br.Get());
    cv.Unclip();
    cv.Stroke(domeGeo.Get(), Mix(kStraw.dark, kShadow, 0.35f), 0.6f, 0.8f * cv.hair);
    const D2D1_POINT_2F bow = {static_cast<float>(-dir) * 0.62f * r, -0.15f * r};
    cv.Oval(Add(bow, -0.1f * r, 0.0f), 0.13f * r, 0.08f * r, Mix(ribbon, kWhite, 0.1f), 1.0f);
    cv.Oval(Add(bow, 0.1f * r, 0.0f), 0.13f * r, 0.08f * r, Mix(ribbon, kShadow, 0.15f), 1.0f);
    cv.Dot(bow, 0.06f * r, Mix(ribbon, kShadow, 0.3f), 1.0f);
}

// ---------------------------------------------------------------------------
// Litter leaves: greys, tinted by their sprites. Each fills its cell, its tip
// up and its stalk down.
// ---------------------------------------------------------------------------

Pts LeafletShape(int shape, float px) {
    Pts pts;
    const float L = 0.46f * px;
    auto bump = [](float p, float c, float w) {
        const float d = (p - c) / w;
        return std::exp(-d * d);
    };
    constexpr int n = 40;
    for (int i = 0; i < n; ++i) {
        const float p = -kPi + kTau * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        float r;
        if (shape == 0) {  // maple: five pointed lobes
            const float l = std::max({bump(p, 0.0f, 0.34f), 0.85f * std::max(bump(p, -1.25f, 0.34f), bump(p, 1.25f, 0.34f)),
                                      0.55f * std::max(bump(p, -2.35f, 0.3f), bump(p, 2.35f, 0.3f))});
            r = L * (0.32f + 0.68f * l);
            pts.push_back({r * std::sin(p), -r * std::cos(p) + 0.1f * L});
        } else if (shape == 1) {  // an oval with a pointed tip
            const float u = (p + kPi) / kTau;  // 0..1 round from the stalk
            const float y = -L * std::cos(kTau * u) * 0.98f;
            const float x = 0.5f * L * std::sin(kTau * u) * (0.92f + 0.08f * std::cos(kTau * u)) *
                            (1.0f - 0.25f * std::pow(Saturate(-std::cos(kTau * u)), 3.0f));
            pts.push_back({x, y});
        } else {  // oak: round lobes along an oblong
            const float u = (p + kPi) / kTau;
            const float y = -L * std::cos(kTau * u) * 0.98f;
            const float t = 0.5f - 0.5f * std::cos(kTau * u);  // 0 at the stalk .. 1 at the tip
            const float half = 0.42f * L * std::pow(std::sin(kPi * t), 0.6f) * (0.72f + 0.28f * std::fabs(std::sin(3.0f * kPi * t)));
            pts.push_back({std::sin(kTau * u) >= 0.0f ? half : -half, y});
        }
    }
    return pts;
}

void PaintLeaflet(const Canvas& cv, int shape, float px) {
    auto g = cv.Path(LeafletShape(shape, px), true);
    if (!g) return;
    const float L = 0.46f * px;
    if (auto br = cv.Radial({-0.15f * L, -0.2f * L}, {0, 0}, 1.1f * L, 1.1f * L,
                            {Stop(0.0f, Rgb{1.0f, 1.0f, 1.0f}), Stop(0.65f, Rgb{0.95f, 0.95f, 0.95f}),
                             Stop(1.0f, Rgb{0.8f, 0.8f, 0.8f})}))
        cv.Fill(g.Get(), br.Get());
    // Veins and an edge, faint on the smallest (where they would only muddy the colour).
    const float w = std::max(0.5f, 0.05f * L), ink = 0.3f + 0.3f * Saturate(px / 40.0f);
    cv.Clip(g.Get());
    cv.Line({0.0f, 0.95f * L}, {0.0f, -0.8f * L}, Rgb{0.62f, 0.62f, 0.62f}, ink + 0.1f, w);
    for (float y : {-0.35f, 0.05f, 0.4f})
        for (float sx : {-1.0f, 1.0f}) cv.Line({0.0f, y * L}, {sx * 0.4f * L, (y - 0.3f) * L}, Rgb{0.7f, 0.7f, 0.7f}, ink, 0.7f * w);
    cv.Unclip();
    cv.Stroke(g.Get(), Rgb{0.5f, 0.5f, 0.5f}, ink, w);
    cv.Line({0.0f, 0.9f * L}, {0.06f * L, 1.08f * L}, Rgb{0.45f, 0.45f, 0.45f}, 0.8f, 1.2f * w);
}

}  // namespace

HRESULT Scenery::BuildPage(ID2D1DeviceContext* dc) {
    const float s = layout_.dpiScale;
    ComPtr<ID2D1Device> device;
    dc->GetDevice(&device);
    if (!device) return E_FAIL;
    // A private context on the same device: the caller is mid-frame.
    ComPtr<ID2D1DeviceContext> rdc;
    HRESULT hr = device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rdc);
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1Factory> factory;
    rdc->GetFactory(&factory);
    if (!factory) return E_FAIL;
    rdc->SetDpi(96.0f, 96.0f);
    rdc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
    rdc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // The faces and lids each carved prop shows (cell px about its anchor).
    struct Carving {
        std::vector<Cut> cuts;
        std::vector<Pts> lid;
        D2D1_RECT_F box = {1e9f, 1e9f, -1e9f, -1e9f};
    };
    auto bound = [](Carving& c) {
        for (const Cut& cut : c.cuts)
            for (const Pts& fig : cut)
                for (const D2D1_POINT_2F& p : fig) {
                    c.box.left = std::min(c.box.left, p.x);
                    c.box.top = std::min(c.box.top, p.y);
                    c.box.right = std::max(c.box.right, p.x);
                    c.box.bottom = std::max(c.box.bottom, p.y);
                }
        for (const Pts& seam : c.lid)
            for (const D2D1_POINT_2F& p : seam) {
                c.box.left = std::min(c.box.left, p.x);
                c.box.top = std::min(c.box.top, p.y);
                c.box.right = std::max(c.box.right, p.x);
                c.box.bottom = std::max(c.box.bottom, p.y);
            }
    };
    std::vector<Carving> carved(pumpkins_.size()), heads(crows_.size());
    if (halloween_) {
        for (size_t i = 0; i < pumpkins_.size(); ++i) {
            const Pumpkin& p = pumpkins_[i];
            if (p.face < 0) continue;
            const D2D1_POINT_2F c = {0.0f, -p.shape.lift};
            carved[i].cuts = PlaceCuts(p.shape, p.face, p.faceTurn, c);
            carved[i].lid = LidSeam(p.shape, c, p.faceTurn);
            bound(carved[i]);
        }
        for (size_t i = 0; i < crows_.size(); ++i) {
            const Scarecrow& m = crows_[i];
            heads[i].cuts = PlaceCuts(m.head, m.face, static_cast<float>(m.dir) * 0.42f, {0.0f, 0.0f});
            bound(heads[i]);
        }
    }

    // ---- Plan the cells ------------------------------------------------------
    struct Box {
        Sprite* sprite;
        int w, h;      // incl. padding
        float ax, ay;  // anchor inside the box
        int kind, prop, part, level;
    };
    enum {
        kBlobCell, kGlowCell, kLeafletCell,                         // shared
        kBodyCell, kLanternCell, kVineCell, kFrondCell,             // pumpkins
        kCrowCell, kHeadCell, kFaceCell, kHatCell, kTailCell, kHandCell  // scarecrows
    };
    std::vector<Box> boxes;
    // Extents around the anchor (px; one may be negative where the art stands
    // off it), padded; the anchor lands on a whole pixel.
    auto box = [&](Sprite* sp, int kind, int prop, int part, int level, float left, float top, float right, float bottom) {
        const int l = static_cast<int>(std::ceil(left)) + kPad, t = static_cast<int>(std::ceil(top)) + kPad;
        const int r = static_cast<int>(std::ceil(right)) + kPad, b = static_cast<int>(std::ceil(bottom)) + kPad;
        if (l + r <= 2 * kPad || t + b <= 2 * kPad) return;
        boxes.push_back({sp, l + r, t + b, static_cast<float>(l), static_cast<float>(t), kind, prop, part, level});
    };
    for (int l = 0; l < kLevels; ++l) {
        const float k = std::exp2(-static_cast<float>(l));
        blobPx_[l] = 48.0f * k;
        glowPx_[l] = 128.0f * k;
        leafletPx_[l] = 40.0f * k;
        box(&blob_[l], kBlobCell, 0, 0, l, 0.5f * blobPx_[l], 0.5f * blobPx_[l], 0.5f * blobPx_[l], 0.5f * blobPx_[l]);
        box(&glow_[l], kGlowCell, 0, 0, l, 0.5f * glowPx_[l], 0.5f * glowPx_[l], 0.5f * glowPx_[l], 0.5f * glowPx_[l]);
        for (int shape = 0; shape < kLeaflets; ++shape)
            box(&leaflet_[shape][l], kLeafletCell, 0, shape, l, 0.5f * leafletPx_[l], 0.5f * leafletPx_[l], 0.5f * leafletPx_[l],
                0.5f * leafletPx_[l]);
    }
    const float back = std::ceil(1.6f * s) + 1.0f;  // room for a soft shadow
    for (size_t i = 0; i < pumpkins_.size(); ++i) {
        Pumpkin& p = pumpkins_[i];
        const int prop = static_cast<int>(i);
        const float R = p.shape.R;
        const float side = std::max(1.18f * R, std::fabs(std::sin(p.stalkLean)) * 1.6f * p.stalkLen + p.stalkW);
        box(&p.body, kBodyCell, prop, 0, 0, side + back + 2.0f, p.H + 0.8f * p.stalkW + 3.0f, side + back + 2.0f, back + 3.0f);
        if (!carved[i].cuts.empty()) {
            const D2D1_RECT_F& b = carved[i].box;
            box(&p.lantern, kLanternCell, prop, 0, 0, -b.left + 3.0f, -b.top + 3.0f, b.right + 3.0f, b.bottom + 3.0f);
        }
        const float curl = p.tendrilLen + 3.0f;
        if (p.tendril != 0) box(&p.vine, kVineCell, prop, 0, 0, curl, curl, curl, curl);
        for (size_t f = 0; f < p.fronds.size(); ++f) {
            const Frond& fr = p.fronds[f];
            const float wide = 0.62f * fr.size + 0.1f * fr.stem + back + 2.0f;
            box(&p.fronds[f].art, kFrondCell, prop, static_cast<int>(f), 0, wide, fr.stem + 0.5f * fr.size + back + 2.0f, wide,
                5.0f * s + back);
        }
    }
    for (size_t i = 0; i < crows_.size(); ++i) {
        Scarecrow& m = crows_[i];
        const int prop = static_cast<int>(i);
        const float S = m.S, r = m.headR;
        const float arms = 0.5f * S + back + 2.0f;
        box(&m.body, kCrowCell, prop, 0, 0, arms, m.knotY + 0.1f * S + 2.0f, arms, 0.07f * S + back + 2.0f);
        box(&m.headArt, kHeadCell, prop, 0, 0, 1.2f * r + back + 2.0f, S - m.headY + 0.05f * S + 2.0f, 1.2f * r + back + 2.0f,
            m.head.lift + 0.1f * S + back + 2.0f);
        if (!heads[i].cuts.empty()) {
            const D2D1_RECT_F& b = heads[i].box;
            box(&m.lantern, kFaceCell, prop, 0, 0, -b.left + 3.0f, -b.top + 3.0f, b.right + 3.0f, b.bottom + 3.0f);
        }
        const int hat = HatOf(m);
        const float wide = (hat == kStrawHat ? 1.86f : (hat == kWitchHat ? 1.62f : 1.24f)) * r + back + 2.0f;
        box(&m.hat, kHatCell, prop, 0, 0, wide, kHatHeight[hat] * r + 0.1f * r + 2.0f, wide, 0.45f * r + back + 2.0f);
        box(&m.tail, kTailCell, prop, 0, 0, 0.09f * S + back + 2.0f, 0.03f * S + 2.0f, 0.1f * S + back + 2.0f,
            0.24f * S + back + 2.0f);
        for (int k = 0; k < 2; ++k)
            box(&m.hand[k], kHandCell, prop, k, 0, 0.04f * S + 2.0f, 0.1f * S + 2.0f, 0.16f * S + 2.0f, 0.1f * S + 2.0f);
    }

    // Shelf-pack, tallest first.
    std::vector<size_t> order(boxes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return boxes[a].h > boxes[b].h; });
    int widest = 0;
    for (const Box& b : boxes) widest = std::max(widest, b.w);
    const UINT32 pageW = widest > 1024 ? 2048u : 1024u;
    UINT32 x = 0, y = 0, row = 0;
    for (size_t i : order) {
        Box& b = boxes[i];
        if (x + static_cast<UINT32>(b.w) > pageW) {
            x = 0;
            y += row;
            row = 0;
        }
        b.sprite->src = {x, y, x + static_cast<UINT32>(b.w), y + static_cast<UINT32>(b.h)};
        b.sprite->cx = static_cast<float>(x) + b.ax;
        b.sprite->cy = static_cast<float>(y) + b.ay;
        x += static_cast<UINT32>(b.w);
        row = std::max(row, static_cast<UINT32>(b.h));
    }
    const UINT32 pageH = y + row;
    if (pageW > rdc->GetMaximumBitmapSize() || pageH > rdc->GetMaximumBitmapSize() || pageH == 0) return E_OUTOFMEMORY;

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    ComPtr<ID2D1Bitmap1> page;
    if (FAILED(hr = rdc->CreateBitmap(D2D1::SizeU(pageW, pageH), nullptr, 0, props, &page))) return hr;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(hr = rdc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush))) return hr;
    ComPtr<ID2D1StrokeStyle> round;
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &round);
    Stops stops(rdc.Get());
    const Canvas cv = {rdc.Get(), factory.Get(), brush.Get(), round.Get(), &stops, s, std::max(1.0f, 0.8f * s)};

    // ---- Draw ------------------------------------------------------------------
    rdc->SetTarget(page.Get());
    rdc->BeginDraw();
    rdc->Clear(D2D1::ColorF(0, 0, 0, 0));
    for (const Box& b : boxes) {
        const D2D1_RECT_U& rc = b.sprite->src;
        rdc->SetTransform(D2D1::Matrix3x2F::Identity());
        const float cb = static_cast<float>(kClearBorder);
        rdc->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(rc.left) + cb, static_cast<float>(rc.top) + cb,
                                             static_cast<float>(rc.right) - cb, static_cast<float>(rc.bottom) - cb),
                                 D2D1_ANTIALIAS_MODE_ALIASED);
        rdc->SetTransform(D2D1::Matrix3x2F::Translation(b.sprite->cx, b.sprite->cy));
        Rng rng(0x5CE7Eu + static_cast<uint64_t>(b.kind) * 1013u + static_cast<uint64_t>(b.prop) * 7919u +
                static_cast<uint64_t>(b.part) * 31u);
        switch (b.kind) {
        case kBlobCell: {
            // Solid most of the way out, then soft, so neighbouring blobs merge.
            const float r = 0.5f * blobPx_[b.level];
            if (auto g = cv.Radial({0, 0}, {0, 0}, r, r,
                                   {{0.0f, D2D1::ColorF(1, 1, 1, 1)}, {0.55f, D2D1::ColorF(1, 1, 1, 0.85f)},
                                    {0.85f, D2D1::ColorF(1, 1, 1, 0.35f)}, {1.0f, D2D1::ColorF(1, 1, 1, 0)}}))
                rdc->FillEllipse(D2D1::Ellipse({0, 0}, r, r), g.Get());
            break;
        }
        case kGlowCell: {
            // A soft round glow, (1 - r^2)^3; tints make it add light.
            const float r = 0.5f * glowPx_[b.level];
            if (auto g = cv.Radial({0, 0}, {0, 0}, r, r,
                                   {{0.0f, D2D1::ColorF(1, 1, 1, 1)}, {0.2f, D2D1::ColorF(1, 1, 1, 0.885f)},
                                    {0.4f, D2D1::ColorF(1, 1, 1, 0.593f)}, {0.6f, D2D1::ColorF(1, 1, 1, 0.262f)},
                                    {0.8f, D2D1::ColorF(1, 1, 1, 0.047f)}, {1.0f, D2D1::ColorF(1, 1, 1, 0)}}))
                rdc->FillEllipse(D2D1::Ellipse({0, 0}, r, r), g.Get());
            break;
        }
        case kLeafletCell: PaintLeaflet(cv, b.part, leafletPx_[b.level]); break;
        case kBodyCell: {
            const Pumpkin& p = pumpkins_[static_cast<size_t>(b.prop)];
            const D2D1_POINT_2F c = {0.0f, -p.shape.lift};
            PaintGourd(cv, p.shape, c, kRinds[p.rind], p.rind == kHeirloom);
            const Carving& carving = carved[static_cast<size_t>(b.prop)];
            if (!carving.cuts.empty()) PaintCuts(cv, carving.cuts, carving.lid);
            PaintStalk(cv, {0.0f, -p.stalkY}, p.stalkLen, p.stalkLean, p.stalkW, kStalks[p.stalk]);
            break;
        }
        case kLanternCell: {
            const Pumpkin& p = pumpkins_[static_cast<size_t>(b.prop)];
            const Carving& carving = carved[static_cast<size_t>(b.prop)];
            PaintLantern(cv, carving.cuts, carving.lid, {p.faceX, -(p.shape.lift - 0.35f * p.shape.B)}, p.shape.R, p.faceTurn);
            break;
        }
        case kVineCell: {
            const Pumpkin& p = pumpkins_[static_cast<size_t>(b.prop)];
            PaintTendril(cv, p.tendrilLen, p.tendril, p.stalk == 0 ? kVine : kVineDry);
            break;
        }
        case kFrondCell: {
            const Pumpkin& p = pumpkins_[static_cast<size_t>(b.prop)];
            PaintFrond(cv, p.fronds[static_cast<size_t>(b.part)], rng);
            break;
        }
        case kCrowCell: PaintScarecrow(cv, crows_[static_cast<size_t>(b.prop)], rng); break;
        case kHeadCell: {
            const Scarecrow& m = crows_[static_cast<size_t>(b.prop)];
            if (halloween_) {
                PaintGourd(cv, m.head, {0.0f, 0.0f}, kRinds[b.prop % 2 == 0 ? 0 : 2], false);
                PaintCuts(cv, heads[static_cast<size_t>(b.prop)].cuts, {});
            } else {
                PaintSack(cv, m, rng);
            }
            break;
        }
        case kFaceCell: {
            const Scarecrow& m = crows_[static_cast<size_t>(b.prop)];
            const float turn = static_cast<float>(m.dir) * 0.42f;
            const Vec3 f = m.head.Face(turn, 0.0f, 0.3f);
            PaintLantern(cv, heads[static_cast<size_t>(b.prop)].cuts, {}, {f.x, -f.y + 0.2f * m.head.B}, m.headR, turn);
            break;
        }
        case kHatCell: {
            const Scarecrow& m = crows_[static_cast<size_t>(b.prop)];
            const int hat = HatOf(m);
            if (hat == kWitchHat) PaintWitchHat(cv, m.headR, kWitchHats[m.witch], m.dir);
            else if (hat == kBatteredHat) PaintTopHat(cv, m.headR, m.dir, m.cloth, rng);
            else PaintStrawHat(cv, m.headR, kHatRibbons[m.ribbon], m.dir, rng);
            break;
        }
        case kTailCell: PaintTail(cv, crows_[static_cast<size_t>(b.prop)], rng); break;
        case kHandCell: PaintHand(cv, crows_[static_cast<size_t>(b.prop)].S, rng); break;
        default: break;
        }
        rdc->SetTransform(D2D1::Matrix3x2F::Identity());
        rdc->PopAxisAlignedClip();
    }
    hr = rdc->EndDraw();
    rdc->SetTarget(nullptr);
    if (FAILED(hr)) return hr;
    page_ = std::move(page);
    return S_OK;
}
