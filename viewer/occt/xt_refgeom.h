/* xt_refgeom.h - evaluate Parasolid XT curves / surfaces straight from the formulas of the Parasolid XT Format
 * Reference (Oct 2006; ref page numbers below), independent of OCCT's parametrisations. Used by occt_xt.cpp (natural
 * normal -> FACE.sense) and by occt_xt_check.cpp (oracle for written geometry).
 *   LINE      R(t) = P + t D                                              ref 29
 *   CIRCLE    R(t) = C + r X cos t + r Y sin t, Y = N x X                 ref 30
 *   ELLIPSE   R(t) = C + a X cos t + b Y sin t, Y = N x X                 ref 32
 *   B_CURVE   NURBS, distinct knots + multiplicities, vertex_dim 4 = homogeneous (x w, y w, z w, w)   ref 33-38
 *   PLANE     R(u,v) = P + u X + v Y, Y = N x X                           ref 52
 *   CYLINDER  R(u,v) = P + r X cos u + r Y sin u + v A, Y = A x X         ref 53
 *   CONE      R(u,v) = P + v A + (X cos u + Y sin u)(r + v tan a), Y = A x X, a = half angle
 *             (ref 55 prints "P - v A" and "the axis points away from the half in use"; Mastercam's own XT data says
 *             the opposite: in 164 cone faces of the internet sample parts every vertex fits only "+ v A" (radius grows
 *             along A), and 210 / 210 band faces between two rings obey the loop rule with this sign and the natural
 *             normal dR/du x dR/dv: ~/mastercam-re/work/occt/netx/cone.py, orient3.py)
 *   SPHERE    R(u,v) = C + (X cos u + Y sin u) r cos v + r A sin v, Y = A x X               ref 56
 *   TORUS     R(u,v) = C + (X cos u + Y sin u)(a + b cos v) + b A sin v, Y = A x X          ref 58
 *   natural normal = dR/du x dR/dv; natural curve tangent = dR/dt                    ref 78
 * B_SURFACE vertex order: u-major, vertex (i, j) at index i * n_v + j (not stated in the reference; decoded from the
 * internet sample parts: 221 B-surface faces fit only this order, 0 only the other, 458 either - netx/conv.py). */
#ifndef XT_REFGEOM_H
#define XT_REFGEOM_H
#include <cmath>
#include <vector>
#include <gp_XYZ.hxx>

namespace xtref {

/* NURBS in one parameter, expanded knot vector, homogeneous vertices (dim 3 or 4) */
struct Nurbs1 {
    int deg = 0, dim = 3;
    std::vector<double> knots;   /* expanded (each distinct knot repeated mult times) */
    std::vector<double> v;       /* n * dim */
    int n() const { return (int)v.size() / dim; }
    /* de Boor on the expanded knots; t clamped to [knots[deg], knots[n]] */
    void eval(double t, double out[4]) const {
        int p = deg, nn = n();
        double lo = knots[p], hi = knots[nn];
        if (t < lo) t = lo;
        if (t > hi) t = hi;
        int k = p;
        while (k < nn - 1 && t >= knots[k + 1]) k++;
        double d[32][4];
        for (int j = 0; j <= p; j++) for (int c = 0; c < 4; c++) d[j][c] = c < dim ? v[(k - p + j) * dim + c] : 1.0;
        for (int r = 1; r <= p; r++)
            for (int j = p; j >= r; j--) {
                int i = k - p + j;
                double den = knots[i + p - r + 1] - knots[i];
                double a = den > 0 ? (t - knots[i]) / den : 0;
                for (int c = 0; c < 4; c++) d[j][c] = (1 - a) * d[j - 1][c] + a * d[j][c];
            }
        for (int c = 0; c < 4; c++) out[c] = d[p][c];
    }
};
inline std::vector<double> expand(const std::vector<double> &k, const std::vector<int> &m) {
    std::vector<double> e;
    for (size_t i = 0; i < k.size(); i++) for (int j = 0; j < m[i]; j++) e.push_back(k[i]);
    return e;
}

struct Curve {
    enum { LINE, CIRCLE, ELLIPSE, BCURVE } kind = LINE;
    gp_XYZ p, d, n, x;           /* LINE p,d; CIRCLE/ELLIPSE centre p, normal n, x_axis x */
    double r = 0, r2 = 0;
    Nurbs1 b; bool rational = false;
    gp_XYZ eval(double t) const {
        switch (kind) {
        case LINE: return p + d * t;
        case CIRCLE: { gp_XYZ y = n.Crossed(x); return p + x * (r * cos(t)) + y * (r * sin(t)); }
        case ELLIPSE: { gp_XYZ y = n.Crossed(x); return p + x * (r * cos(t)) + y * (r2 * sin(t)); }
        default: { double o[4]; b.eval(t, o); double w = rational ? o[3] : 1; return gp_XYZ(o[0] / w, o[1] / w, o[2] / w); }
        }
    }
};

struct Surface {
    enum { PLANE, CYLINDER, CONE, SPHERE, TORUS, BSURF } kind = PLANE;
    gp_XYZ p, a, x;              /* pvec / centre, normal / axis, x_axis */
    double r = 0, r2 = 0, sa = 0, ca = 1;
    /* B_SURFACE */
    int udeg = 0, vdeg = 0, nu = 0, nv = 0, dim = 3; bool rational = false, vmajor = false;
    std::vector<double> uk, vk, verts;   /* expanded knots, vertices */
    gp_XYZ y() const { return a.Crossed(x); }
    gp_XYZ eval(double u, double v) const {
        switch (kind) {
        case PLANE: return p + x * u + y() * v;
        case CYLINDER: return p + x * (r * cos(u)) + y() * (r * sin(u)) + a * v;
        case CONE: return p + a * v + (x * cos(u) + y() * sin(u)) * (r + v * (sa / ca));
        case SPHERE: return p + (x * cos(u) + y() * sin(u)) * (r * cos(v)) + a * (r * sin(v));
        case TORUS: return p + (x * cos(u) + y() * sin(u)) * (r + r2 * cos(v)) + a * (r2 * sin(v));
        default: {
            /* evaluate each row of vertices in the second direction, then the first */
            Nurbs1 cu; cu.deg = udeg; cu.dim = 4; cu.knots = uk;
            for (int i = 0; i < nu; i++) {
                Nurbs1 cv; cv.deg = vdeg; cv.dim = dim; cv.knots = vk;
                for (int j = 0; j < nv; j++) {
                    int idx = vmajor ? (j * nu + i) : (i * nv + j);
                    for (int c = 0; c < dim; c++) cv.v.push_back(verts[idx * dim + c]);
                }
                double o[4]; cv.eval(v, o);
                for (int c = 0; c < 4; c++) cu.v.push_back(o[c]);
            }
            double o[4]; cu.eval(u, o);
            double w = rational ? o[3] : 1;
            return gp_XYZ(o[0] / w, o[1] / w, o[2] / w);
        }
        }
    }
    /* (u, v) of a point on an analytic surface (closed forms from the formulas above); false for B_SURFACE */
    bool invert(const gp_XYZ &q, double &u, double &v) const {
        gp_XYZ d = q - p, Y = y();
        switch (kind) {
        case PLANE: u = d.Dot(x); v = d.Dot(Y); return true;
        case CYLINDER: v = d.Dot(a); u = atan2(d.Dot(Y), d.Dot(x)); return true;
        case CONE: v = d.Dot(a); u = atan2(d.Dot(Y), d.Dot(x)); return true;
        case SPHERE: { double h = d.Dot(a); gp_XYZ rad = d - a * h; u = atan2(rad.Dot(Y), rad.Dot(x)); v = atan2(h, rad.Modulus()); return true; }
        case TORUS: {
            double h = d.Dot(a); gp_XYZ rad = d - a * h;
            u = atan2(rad.Dot(Y), rad.Dot(x));
            v = atan2(h, rad.Modulus() - r); return true;
        }
        default: return false;
        }
    }
    gp_XYZ normal(double u, double v, double h = 1e-6) const {   /* natural normal, central differences */
        gp_XYZ du = eval(u + h, v) - eval(u - h, v), dv = eval(u, v + h) - eval(u, v - h);
        return du.Crossed(dv);
    }
};

}  // namespace xtref
#endif
