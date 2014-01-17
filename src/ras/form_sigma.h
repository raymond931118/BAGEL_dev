//
// BAGEL - Parallel electron correlation program.
// Filename: ras/form_sigma.h
// Copyright (C) 2013 Toru Shiozaki
//
// Author: Shane Parker <shane.parker@u.northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// The BAGEL package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the BAGEL package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//

#ifndef __BAGEL_RAS_FORM_SIGMA_RAS_H
#define __BAGEL_RAS_FORM_SIGMA_RAS_H

#include <src/ras/civector.h>
#include <src/fci/mofile.h>

namespace bagel {

class FormSigmaRAS {
  public:
    FormSigmaRAS() {}

    // This is really all this class is
    std::shared_ptr<RASDvec> operator()(std::shared_ptr<const RASDvec> cc, std::shared_ptr<const MOFile> jop, const std::vector<int>& conv) const;
    std::shared_ptr<RASDvec> operator()(std::shared_ptr<const RASDvec> cc, const double* mo1e) const;

  private:
    // Helper functions for sigma formation
    void sigma_aa(std::shared_ptr<const RASCivec> cc, std::shared_ptr<RASCivec> sigma, const double* g, const double* mo2e) const;
    void sigma_bb(std::shared_ptr<const RASCivec> cc, std::shared_ptr<RASCivec> sigma, const double* g, const double* mo2e) const;
    void sigma_ab(std::shared_ptr<const RASCivec> cc, std::shared_ptr<RASCivec> sigma, const double* mo2e) const;

    //void sigma_ab_1(std::shared_ptr<const RASCivec> cc, std::shared_ptr<RASCivec> sigma, const double* mo2e) const;
};

}

#endif

