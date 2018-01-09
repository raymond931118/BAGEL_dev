//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: asd_dmrg_orbopt.cc
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

#include <src/asd/dmrg/orbopt/asd_dmrg_orbopt.h>
#include <src/scf/hf/fock.h>

using namespace std;
using namespace bagel;

ASD_DMRG_OrbOpt::ASD_DMRG_OrbOpt(shared_ptr<const PTree> idata, shared_ptr<const Reference> iref) : input_(idata) {
  
  print_header();
  
  max_iter_ = input_->get<int>("opt_maxiter", 50);
  max_micro_iter_ = input_->get<int>("opt_max_micro_iter", 100);
  thresh_ = input_->get<double>("opt_thresh", 1.0e-8); // thresh for macro iteration

  // collect ASD-DMRG info
  auto asd_dmrg_info = input_->get_child_optional("asd_dmrg_info");
  if (!asd_dmrg_info) throw runtime_error("ASD-DMRG info has to be provided for orbital optimization");
  // construct ASD-DMRG
  asd_dmrg_ = make_shared<RASD>(asd_dmrg_info, iref);

  cout << "    * nstate   : " << setw(6) << asd_dmrg_->nstate() << endl;
  cout << "    * nclosed  : " << setw(6) << asd_dmrg_->sref()->nclosed() << endl;
  cout << "    * nact     : " << setw(6) << asd_dmrg_->sref()->nact() << endl;
  cout << "    * nvirt    : " << setw(6) << asd_dmrg_->sref()->nvirt() << endl << endl;
  assert(asd_dmrg_->sref()->nact() && asd_dmrg_->sref()->nvirt());

#ifdef AAROT
  cout << " *** Active-active rotation turned on!" << endl;
  // initialize active-active rotation parameters
  int offset = 0;
  for (int sj = 0; sj != asd_dmrg_->nsites()-1; ++sj)
    act_rotblocks_.emplace_back(asd_dmrg_->active_sizes(), sj, offset);
  naa_ = offset;
#else
  naa_ = 0;
#endif

  cout << "  ===== Orbital Optimization Iteration =====" << endl << endl;
}


void ASD_DMRG_OrbOpt::print_header() const {
  cout << "  --------------------------------------------------" << endl;
  cout << "     ASD-DMRG Second Order Orbital Optimization     " << endl;
  cout << "  --------------------------------------------------" << endl << endl;
}


void ASD_DMRG_OrbOpt::print_iteration(const int iter, const vector<double>& energy, const double error) const {
  if (energy.size() != 1 && iter) cout << endl;

  int i = 0;
  for (auto& e : energy) {
    cout << "  " << setw(5) << iter << setw(3) << i << setw(19) << fixed << setprecision(12) << e << "   "
                 << setw(10) << scientific << setprecision(2) << (i==0 ? error : 0.0) << endl;
    ++i;
  }
}


shared_ptr<Matrix> ASD_DMRG_OrbOpt::compute_active_fock(const MatView acoeff, shared_ptr<const RDM<1>> rdm1) const {
  auto sref = asd_dmrg_->sref();
  const int nact = asd_dmrg_->sref()->nact();
  Matrix dkl(nact, nact);
  copy_n(rdm1->data(), dkl.size(), dkl.data());
  dkl.sqrt();
  dkl.scale(1.0/sqrt(2.0));
  return make_shared<Fock<1>>(sref->geom(), sref->hcore()->clone(), nullptr, acoeff*dkl, false, true);
}


shared_ptr<Matrix> ASD_DMRG_OrbOpt::compute_qvec(const MatView acoeff, shared_ptr<const RDM<2>> rdm2) const {
  
  auto sref = asd_dmrg_->sref();
  const int nocc = sref->nclosed() + sref->nact();
  auto half = sref->geom()->df()->compute_half_transform(acoeff);

  // TODO MPI modification
  shared_ptr<const DFFullDist> full = half->apply_JJ()->compute_second_transform(coeff_->slice(sref->nclosed(), nocc));

  // [D|tu] = (D|xy) Gamma_{xy,tu}
  shared_ptr<const DFFullDist> prdm = full->apply_2rdm(*rdm2);

  // (r,u) = (rt|D) [D|tu]
  shared_ptr<const Matrix> tmp = half->form_2index(prdm, 1.0);

  return make_shared<Matrix>(*coeff_ % *tmp);
}


shared_ptr<const Coeff> ASD_DMRG_OrbOpt::semi_canonical_orb() const {
  auto sref = asd_dmrg_->sref();
  const int nclosed = sref->nclosed();
  const int nact = sref->nact();
  const int nocc = nclosed + nact;
  const int nvirt = sref->nvirt();
  const int norb = coeff_->mdim();
  
  auto rdm1_mat = make_shared<Matrix>(nact, nact);
  copy_n(asd_dmrg_->rdm1_av()->data(), rdm1_mat->size(), rdm1_mat->data());
  rdm1_mat->sqrt();
  rdm1_mat->scale(1.0/sqrt(2.0));

  const MatView ccoeff = coeff_->slice(0, nclosed);
  const MatView acoeff = coeff_->slice(nclosed, nocc);
  const MatView vcoeff = coeff_->slice(nocc, norb);

  VectorB eig(coeff_->mdim());
  auto core_fock = nclosed ? make_shared<Fock<1>>(sref->geom(), sref->hcore(), nullptr, coeff_->slice(0, nclosed), false, true)
                            : make_shared<Matrix>(*sref->hcore());
  Fock<1> fock(sref->geom(), core_fock, nullptr, acoeff * *rdm1_mat, false, true);

  Matrix trans(norb, norb);
  trans.unit();
  if (nclosed) {
    Matrix ofock = ccoeff % fock * ccoeff;
    ofock.diagonalize(eig);
    trans.copy_block(0, 0, nclosed, nclosed, ofock);
  }
  Matrix vfock = vcoeff % fock * vcoeff;
  vfock.diagonalize(eig);
  trans.copy_block(nocc, nocc, nvirt, nvirt, vfock);
  
  return make_shared<Coeff>(*coeff_ * trans);
}


