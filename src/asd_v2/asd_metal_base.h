//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: asd_metal_base.h
// Copyright (C) 2017 Raymond Wang 
//
// Author: Raymond Wang <raymondwang@u.northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __SRC_ASD_V2_ASD_METAL_BASE_H
#define __SRC_ASD_V2_ASD_METAL_BASE_H

#include <src/asd_v2/multimer/multimer.h>

namespace bagel {

class ASD_Metal_base {
  protected:
    std::shared_ptr<const Multimer> multimer_;

  public:
    // constructors
    ASD_Metal_base(const std::shared_ptr<const PTree> itree, std::shared_ptr<const Geometry> geom);

    // utility functions
    std::shared_ptr<const Multimer> multimer() const { return multimer_; }
    
};

}

#endif
