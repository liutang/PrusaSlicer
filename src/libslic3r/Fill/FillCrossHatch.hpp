///|/ Copyright (c) Bambu Lab, OrcaSlicer contributors
///|/
///|/ Ported from OrcaSlicer (src/libslic3r/Fill/FillCrossHatch.hpp).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FillCrossHatch_hpp_
#define slic3r_FillCrossHatch_hpp_

#include <utility>

#include "libslic3r/libslic3r.h"
#include "FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {
class Point;

class FillCrossHatch : public Fill
{
public:
    Fill* clone() const override { return new FillCrossHatch(*this); }
    ~FillCrossHatch() override {}
    bool is_self_crossing() override { return false; }

protected:
    void _fill_surface_single(
        const FillParams                &params,
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction,
        ExPolygon                        expolygon,
        Polylines                       &polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_FillCrossHatch_hpp_
