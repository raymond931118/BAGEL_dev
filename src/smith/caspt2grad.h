//
// BAGEL - Parallel electron correlation program.
// Filename: caspt2grad.h
// Copyright (C) 2013 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
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


#ifndef __SRC_SMITH_CASPT2GRAD_H
#define __SRC_SMITH_CASPT2GRAD_H

#include <src/wfn/reference.h>
#include <src/wfn/method.h>
#include <src/input/input.h>
#include <src/casscf/casscf.h>

namespace bagel {

class CASPT2Grad : public Method {
  protected:
    std::shared_ptr<const Matrix> yrs_;
    std::shared_ptr<const Matrix> coeff_;

    std::shared_ptr<FCI> fci_;

    // for gradient
    int target_;
    int ncore_;

  public:
    CASPT2Grad(std::shared_ptr<const PTree>, std::shared_ptr<const Geometry>, std::shared_ptr<const Reference>);

    void compute() override;

    std::shared_ptr<const Matrix> coeff() const { return coeff_; }
    std::shared_ptr<FCI> fci() const { return fci_; }
    int target() const { return target_; }
    int ncore() const { return ncore(); }

    std::shared_ptr<const Reference> conv_to_ref() const override { return ref_; }

    std::tuple<std::shared_ptr<Matrix>,std::shared_ptr<const DFFullDist>>
      compute_y(std::shared_ptr<const Matrix> dm1, std::shared_ptr<const Matrix> dm2, std::shared_ptr<const Civec> cider,
                std::shared_ptr<const DFHalfDist> half, std::shared_ptr<const DFHalfDist> halfj, std::shared_ptr<const DFHalfDist> halfjj);

    std::tuple<std::shared_ptr<const Matrix>, std::shared_ptr<const Matrix>, std::shared_ptr<const Civec>> process_smith();
};

}

#endif
