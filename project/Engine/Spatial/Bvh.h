#pragma once
// Reusable bounding-volume hierarchy for broadphase, frustum culling and ray/overlap queries.
//
// Design (persistent + lazy-rebuild):
//   • Each object is a stable "proxy" (id never changes) holding a TIGHT box and a fattened box.
//   • MoveProxy() only marks the tree dirty when the tight box leaves its fat box, so objects that
//     stay put (or barely move) cost nothing and queries can run without any rebuild. This is the
//     classic persistence trick: a static scene with a moving camera never rebuilds.
//   • When dirty, the internal node tree is rebuilt top-down from the live proxies on the next
//     query (EnsureBuilt). Rebuild is O(n log n); a static structure is trivial to keep correct.
//
// The tree bounds FAT boxes, so a query is a conservative superset; leaf tests use the TIGHT box.
#include "Engine/Math/Math.h"
#include <vector>
#include <cfloat>
#include <algorithm>

namespace Fujin {

// Members are named lo/hi (not min/max) and std::min/std::max are parenthesised, because this
// header is pulled into translation units that include <windows.h>, whose min/max macros would
// otherwise clobber both.
struct Aabb {
    Vector3 lo{  FLT_MAX,  FLT_MAX,  FLT_MAX };
    Vector3 hi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

    Aabb() = default;
    Aabb(const Vector3& mn, const Vector3& mx) : lo(mn), hi(mx) {}

    bool Overlaps(const Aabb& o) const {
        return lo.x <= o.hi.x && hi.x >= o.lo.x
            && lo.y <= o.hi.y && hi.y >= o.lo.y
            && lo.z <= o.hi.z && hi.z >= o.lo.z;
    }
    bool Contains(const Aabb& o) const {
        return lo.x <= o.lo.x && hi.x >= o.hi.x
            && lo.y <= o.lo.y && hi.y >= o.hi.y
            && lo.z <= o.lo.z && hi.z >= o.hi.z;
    }
    void Expand(float m) {
        lo.x -= m; lo.y -= m; lo.z -= m;
        hi.x += m; hi.y += m; hi.z += m;
    }
    Vector3 Center() const { return (lo + hi) * 0.5f; }

    static Aabb Union(const Aabb& a, const Aabb& b) {
        return Aabb(Vector3((std::min)(a.lo.x,b.lo.x), (std::min)(a.lo.y,b.lo.y), (std::min)(a.lo.z,b.lo.z)),
                    Vector3((std::max)(a.hi.x,b.hi.x), (std::max)(a.hi.y,b.hi.y), (std::max)(a.hi.z,b.hi.z)));
    }
};

// A frustum plane.  A point p is inside the half-space when  dot(n, p) + d >= 0.
struct Plane { Vector3 n{0,1,0}; float d = 0.0f; };

// Extract the 6 frustum planes from a view-projection matrix that maps a COLUMN vector
// clip = M * worldPos (the engine's Matrix4x4 convention), with D3D clip space z in [0,1].
// Gribb–Hartmann: planes are sums/differences of the matrix rows.
inline void ExtractFrustumPlanes(const Matrix4x4& m, Plane out[6]) {
    Vector4 r0(m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3]);
    Vector4 r1(m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3]);
    Vector4 r2(m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3]);
    Vector4 r3(m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]);
    Vector4 raw[6] = {
        r3 + r0,   // left   ( x >= -w )
        r3 - r0,   // right  ( x <=  w )
        r3 + r1,   // bottom ( y >= -w )
        r3 - r1,   // top    ( y <=  w )
        r2,        // near   ( z >=  0 )  D3D
        r3 - r2,   // far    ( z <=  w )
    };
    for (int i = 0; i < 6; ++i) {
        Vector3 n(raw[i].x, raw[i].y, raw[i].z);
        float len = n.Length();
        if (len > 1e-8f) { out[i].n = n / len; out[i].d = raw[i].w / len; }
        else             { out[i].n = Vector3(0,1,0); out[i].d = 1e9f; }   // degenerate → never culls
    }
}

// True if the box is at least partially inside the frustum (conservative: may keep a few
// boxes that only intersect a plane's slab outside the frustum corners — never culls a
// visible box).
inline bool AabbInFrustum(const Aabb& b, const Plane planes[6]) {
    for (int i = 0; i < 6; ++i) {
        const Plane& pl = planes[i];
        // Positive vertex: the corner farthest along +n. If even it is behind the plane,
        // the whole box is outside.
        Vector3 pv(pl.n.x >= 0 ? b.hi.x : b.lo.x,
                   pl.n.y >= 0 ? b.hi.y : b.lo.y,
                   pl.n.z >= 0 ? b.hi.z : b.lo.z);
        if (Vector3::Dot(pl.n, pv) + pl.d < 0.0f) return false;
    }
    return true;
}

class Bvh {
public:
    // Create / destroy / move proxies. proxy ids are stable across rebuilds.
    int  CreateProxy(const Aabb& tight, void* userData, float margin = 0.05f) {
        int id;
        if (!m_free.empty()) { id = m_free.back(); m_free.pop_back(); }
        else { id = (int)m_leaves.size(); m_leaves.push_back({}); }
        Leaf& lf = m_leaves[(size_t)id];
        lf.tight = tight; lf.fat = tight; lf.fat.Expand(margin);
        lf.data = userData; lf.alive = true;
        m_dirty = true;
        return id;
    }
    void DestroyProxy(int proxy) {
        if (proxy < 0 || proxy >= (int)m_leaves.size() || !m_leaves[(size_t)proxy].alive) return;
        m_leaves[(size_t)proxy].alive = false;
        m_leaves[(size_t)proxy].data  = nullptr;
        m_free.push_back(proxy);
        m_dirty = true;
    }
    // Returns true if the move forced a tree rebuild (tight escaped the fat box).
    bool MoveProxy(int proxy, const Aabb& tight, float margin = 0.05f) {
        Leaf& lf = m_leaves[(size_t)proxy];
        lf.tight = tight;
        if (lf.fat.Contains(tight)) return false;     // still inside the fat box → tree stays valid
        lf.fat = tight; lf.fat.Expand(margin);
        m_dirty = true;
        return true;
    }
    void* GetUserData(int proxy) const { return m_leaves[(size_t)proxy].data; }
    void  Clear() { m_leaves.clear(); m_free.clear(); m_nodes.clear(); m_root = -1; m_dirty = true; }

    // Visit every proxy whose TIGHT box overlaps `box`. cb signature: void(void* userData).
    template<class F>
    void QueryOverlap(const Aabb& box, F&& cb) {
        EnsureBuilt();
        if (m_root < 0) return;
        int stack[64]; int sp = 0; stack[sp++] = m_root;
        while (sp > 0) {
            const Node& nd = m_nodes[(size_t)stack[--sp]];
            if (!nd.box.Overlaps(box)) continue;
            if (nd.leaf >= 0) {
                const Leaf& lf = m_leaves[(size_t)nd.leaf];
                if (lf.alive && lf.tight.Overlaps(box)) cb(lf.data);
            } else {
                if (sp < 62) { stack[sp++] = nd.left; stack[sp++] = nd.right; }
            }
        }
    }

    // Visit every proxy whose box the ray [o, o+d*maxDist] intersects. cb: void(void* userData).
    // The callback does the precise test; the tree only prunes by box.
    template<class F>
    void RayCast(const Vector3& o, const Vector3& d, float maxDist, F&& cb) {
        EnsureBuilt();
        if (m_root < 0) return;
        int stack[64]; int sp = 0; stack[sp++] = m_root;
        while (sp > 0) {
            const Node& nd = m_nodes[(size_t)stack[--sp]];
            if (!RaySlab(nd.box, o, d, maxDist)) continue;
            if (nd.leaf >= 0) {
                const Leaf& lf = m_leaves[(size_t)nd.leaf];
                if (lf.alive && RaySlab(lf.tight, o, d, maxDist)) cb(lf.data);
            } else {
                if (sp < 62) { stack[sp++] = nd.left; stack[sp++] = nd.right; }
            }
        }
    }

    // Visit every proxy whose box is inside the frustum. cb signature: void(void* userData).
    template<class F>
    void QueryFrustum(const Plane planes[6], F&& cb) {
        EnsureBuilt();
        if (m_root < 0) return;
        int stack[64]; int sp = 0; stack[sp++] = m_root;
        while (sp > 0) {
            const Node& nd = m_nodes[(size_t)stack[--sp]];
            if (!AabbInFrustum(nd.box, planes)) continue;     // whole subtree outside → prune
            if (nd.leaf >= 0) {
                const Leaf& lf = m_leaves[(size_t)nd.leaf];
                if (lf.alive && AabbInFrustum(lf.tight, planes)) cb(lf.data);
            } else {
                if (sp < 62) { stack[sp++] = nd.left; stack[sp++] = nd.right; }
            }
        }
    }

private:
    struct Leaf { Aabb fat, tight; void* data = nullptr; bool alive = false; };
    struct Node { Aabb box; int left = -1, right = -1, leaf = -1; };

    std::vector<Leaf> m_leaves;
    std::vector<int>  m_free;
    std::vector<Node> m_nodes;
    int  m_root  = -1;
    bool m_dirty = true;

    void EnsureBuilt() {
        if (!m_dirty) return;
        m_dirty = false;
        m_nodes.clear();
        m_build.clear();
        for (int i = 0; i < (int)m_leaves.size(); ++i)
            if (m_leaves[(size_t)i].alive) m_build.push_back(i);
        m_root = m_build.empty() ? -1 : Build(0, (int)m_build.size());
    }

    // Top-down median split on the longest centroid axis. Node boxes bound FAT leaf boxes.
    int Build(int begin, int end) {
        int ni = (int)m_nodes.size();
        m_nodes.push_back({});
        if (end - begin == 1) {
            int leaf = m_build[(size_t)begin];
            m_nodes[(size_t)ni].box  = m_leaves[(size_t)leaf].fat;
            m_nodes[(size_t)ni].leaf = leaf;
            return ni;
        }
        Aabb box = m_leaves[(size_t)m_build[(size_t)begin]].fat;
        for (int i = begin + 1; i < end; ++i)
            box = Aabb::Union(box, m_leaves[(size_t)m_build[(size_t)i]].fat);
        Vector3 sz = box.hi - box.lo;
        int axis = (sz.y > sz.x && sz.y > sz.z) ? 1 : (sz.z > sz.x) ? 2 : 0;
        int mid = (begin + end) / 2;
        auto centerAxis = [&](int leaf) {
            const Aabb& f = m_leaves[(size_t)leaf].fat;
            return axis == 0 ? f.lo.x + f.hi.x : axis == 1 ? f.lo.y + f.hi.y : f.lo.z + f.hi.z;
        };
        std::nth_element(m_build.begin() + begin, m_build.begin() + mid, m_build.begin() + end,
            [&](int a, int b) { return centerAxis(a) < centerAxis(b); });
        int l = Build(begin, mid);
        int r = Build(mid, end);
        m_nodes[(size_t)ni].box   = Aabb::Union(m_nodes[(size_t)l].box, m_nodes[(size_t)r].box);
        m_nodes[(size_t)ni].left  = l;
        m_nodes[(size_t)ni].right = r;
        return ni;
    }

    // Slab test: does the ray [o, o+d*maxDist] intersect the box? Handles axis-parallel rays.
    static bool RaySlab(const Aabb& b, const Vector3& o, const Vector3& d, float maxDist) {
        float tmin = 0.0f, tmax = maxDist;
        for (int a = 0; a < 3; ++a) {
            float od = a == 0 ? o.x : a == 1 ? o.y : o.z;
            float dd = a == 0 ? d.x : a == 1 ? d.y : d.z;
            float lo = a == 0 ? b.lo.x : a == 1 ? b.lo.y : b.lo.z;
            float hi = a == 0 ? b.hi.x : a == 1 ? b.hi.y : b.hi.z;
            if (dd > -1e-8f && dd < 1e-8f) {            // parallel to this slab
                if (od < lo || od > hi) return false;
            } else {
                float inv = 1.0f / dd;
                float t1 = (lo - od) * inv, t2 = (hi - od) * inv;
                if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            }
        }
        return true;
    }

    std::vector<int> m_build;   // scratch leaf-index list reused by EnsureBuilt/Build
};

} // namespace Fujin
