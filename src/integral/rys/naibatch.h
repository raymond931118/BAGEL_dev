//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: naibatch.h
// Copyright (C) 2009 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
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

#ifndef __SRC_INTEGRAL_RYS_NAIBATCH_H
#define __SRC_INTEGRAL_RYS_NAIBATCH_H

#include <src/integral/rys/coulombbatch_energy.h>

namespace bagel {

class NAIBatch : public CoulombBatch_energy {
  public:
    NAIBatch(const std::array<std::shared_ptr<const Shell>,2>& _info, const std::shared_ptr<const Molecule> mol, std::shared_ptr<StackMem> stack = nullptr);
};

}

#endif

