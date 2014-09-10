//
// BAGEL - Parallel electron correlation program.
// Filename: reference_london.cc
// Copyright (C) 2014 Toru Shiozaki
//
// Author: Ryan D. Reynolds <RyanDReynolds@u.northwestern.edu>
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

#include <src/london/reference_london.h>
#include <src/molecule/mixedbasis.h>
#include <src/molecule/zoverlap.h>
#include <src/integral/compos/complexoverlapbatch.h>

BOOST_CLASS_EXPORT_IMPLEMENT(bagel::Reference_London)

using namespace std;
using namespace bagel;

Reference_London::Reference_London(shared_ptr<const Geometry> g, shared_ptr<const ZCoeff> c,
                                   const int _nclosed, const int _nact, const int _nvirt,
                                   const double en) : Reference() {
  geom_ = g;
  energy_ = en;
  zhcore_ = make_shared<ZHcore>(geom_);
  hcore_ = nullptr;
  nclosed_ = _nclosed;
  nact_ = _nact;
  nvirt_ = _nvirt;
  nstate_ = 1;
  ciwfn_ = nullptr;
  rdm1_ = std::vector<std::shared_ptr<RDM<1>>>();
  rdm2_ = std::vector<std::shared_ptr<RDM<2>>>();
  rdm1_av_ = nullptr;
  rdm2_av_ = nullptr;

  // we need to make sure that all the quantities are consistent in every MPI process
  if (c) {
    mpi__->broadcast(const_pointer_cast<ZCoeff>(c)->data(), c->size(), 0);
    zcoeff_ = c;
    coeff_ = nullptr;
  }
}


shared_ptr<Reference> Reference_London::project_coeff(shared_ptr<const Geometry> geomin) const {

  // project to a new basis
  const ZOverlap snew(geomin);
  ZOverlap snewinv = snew;
  snewinv.inverse();
  MixedBasis<ComplexOverlapBatch, ZMatrix> mixed(geom_, geomin);
  auto c = make_shared<ZCoeff>(snewinv * mixed * *zcoeff_);

  // make coefficient orthogonal (under the overlap metric)
  ZMatrix unit = *c % snew * *c;
  unit.inverse_half();
  *c *= unit;

  auto out = make_shared<Reference_London>(geomin, c, nclosed_, nact_, zcoeff_->mdim()-nclosed_-nact_, energy_);
  if (coeffA_ || coeffB_) throw runtime_error("UHF with GIAO basis has not been implemented.");
  return out;
}
