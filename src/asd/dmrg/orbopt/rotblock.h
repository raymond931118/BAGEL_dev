//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: rotblock.h
// Copyright (C) 2017 Raymond Wang
//
// Author: Raymond Wang <raymondwang@u.northwestern.edu>
// Maintainer: Shiozaki Group
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

#ifndef BAGEL_ASD_DMRG_ORBOPT_ROTBLOCK_H
#define BAGEL_ASD_DMRG_ORBOPT_ROTBLOCK_H

#include <src/asd/multisite/multisite.h>

namespace bagel {

struct ASD_RotBlock {
    std::shared_ptr<const MultiSite> multisite_;
    
    int iorbstart;
    int norb_i;
    int jorbstart;
    int norb_j;
    int offset;
  
    ASD_RotBlock(std::shared_ptr<const MultiSite> multisite, const int si, const int sj, int& off) : multisite_(multisite), offset(off) {
      norb_i = multisite_->active_sizes().at(si);
      norb_j = multisite_->active_sizes().at(sj);
      iorbstart = accumulate(multisite_->active_sizes().begin(), multisite_->active_sizes().begin()+si, 0);
      jorbstart = accumulate(multisite_->active_sizes().begin(), multisite_->active_sizes().begin()+sj, 0);
      off += norb_i * norb_j;
    }
};

}


#endif
