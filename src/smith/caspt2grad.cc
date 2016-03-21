//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: caspt2grad.cc
// Copyright (C) 2013 Toru Shiozaki
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

#include <bagel_config.h>

#include <src/scf/hf/fock.h>
#include <src/grad/cpcasscf.h>
#include <src/grad/gradeval.h>
#include <src/multi/casscf/cashybrid.h>
#include <src/multi/casscf/qvec.h>
#include <src/smith/smith.h>
#include <src/smith/caspt2grad.h>
#include <src/prop/multipole.h>
#include <src/prop/hyperfine.h>


using namespace std;
using namespace bagel;

CASPT2Grad::CASPT2Grad(shared_ptr<const PTree> inp, shared_ptr<const Geometry> geom, shared_ptr<const Reference> ref)
  : Method(inp, geom, ref) {
#ifdef COMPILE_SMITH
  Timer timer;

  // compute CASSCF first
  auto cas = make_shared<CASHybrid>(inp, geom, ref);
  cas->compute();

  // update reference
  ref_ = cas->conv_to_ref();
  fci_ = cas->fci();
  thresh_ = cas->thresh();
  ref_energy_ = cas->energy();

  // property calculation
  do_hyperfine_ = inp->get<bool>("hyperfine", false);

  timer.tick_print("Reference calculation");

  cout << endl << "  === DF-CASPT2Grad calculation ===" << endl << endl;
#else
  throw logic_error("CASPT2 gradients require SMITH-generated code. Please compile BAGEL with --enable-smith");
#endif
}


// compute smith and set rdms and ci deriv to a member
void CASPT2Grad::compute() {
#ifdef COMPILE_SMITH
  const int nclosed = ref_->nclosed();
  const int nact = ref_->nact();
  {
    // construct SMITH here
    shared_ptr<PTree> smithinput = idata_->get_child("smith");
    smithinput->put<bool>("grad", true);
    auto smith = make_shared<Smith>(smithinput, ref_->geom(), ref_);
    smith->compute();

    // use coefficients from smith (closed and virtual parts have been rotated in smith to make them canonical).
    coeff_ = smith->coeff();

    if (nact) {
      cideriv_ = smith->cideriv()->copy();
    }
    target_ = smith->algo()->info()->target();
    ncore_  = smith->algo()->info()->ncore();

    Timer timer;

    // save correlated density matrices d(1), d(2), and ci derivatives
    auto d1tmp = make_shared<Matrix>(*smith->dm1());
    auto d11tmp = make_shared<Matrix>(*smith->dm11());
    d11tmp->symmetrize();
    // a factor of 2 from the Hylleraas functional (which is not included in the generated code)
    d11tmp->scale(2.0);

    // d_1^(2) -= <1|1><0|E_mn|0>     [Celani-Werner Eq. (A6)]
    if (nact) {
      const double wf1norm = smith->wf1norm();
      shared_ptr<const Matrix> d0 = ref_->rdm1_mat(target_);
      for (int i = nclosed; i != nclosed+nact; ++i)
        for (int j = nclosed; j != nclosed+nact; ++j)
          d1tmp->element(j-ncore_, i-ncore_) -=  wf1norm * d0->element(j, i);
    }
    if (!ncore_) {
      d1_ = d1tmp;
      d11_ = d11tmp;
    } else {
      auto d1tmp2 = make_shared<Matrix>(coeff_->mdim(), coeff_->mdim());
      d1tmp2->copy_block(ncore_, ncore_, coeff_->mdim()-ncore_, coeff_->mdim()-ncore_, d1tmp);
      d1_ = d1tmp2->copy();
      d1tmp2->copy_block(ncore_, ncore_, coeff_->mdim()-ncore_, coeff_->mdim()-ncore_, d11tmp);
      d11_ = d1tmp2;
    }

    // correct cideriv for fock derivative [Celani-Werner Eq. (C1), some terms in first and second lines]
    // y_I += (g[d^(2)]_ij - Nf_ij) <I|E_ij|0>
    // -> y_I += [(h+g[d^(0)+d^(2)]) - (1+N)F] <I|E_ij|0>
    if (nact) {
      const MatView acoeff = coeff_->slice(nclosed, nclosed+nact);

      auto focksub = [&](shared_ptr<const Matrix> moden, const MatView coeff) {
        shared_ptr<const Matrix> jop = ref_->geom()->df()->compute_Jop(make_shared<Matrix>(coeff * *moden ^ coeff));
        auto out = make_shared<Matrix>(acoeff % (*ref_->hcore() + *jop) * acoeff);
        shared_ptr<const DFFullDist> full = ref_->geom()->df()->compute_half_transform(acoeff)->compute_second_transform(coeff)->apply_J()->swap();
        shared_ptr<DFFullDist> full2 = full->copy();
        full2->rotate_occ1(moden);
        *out += *full->form_2index(full2, -0.5);
        return out;
      };

      const int nmo = coeff_->mdim();
      shared_ptr<const Matrix> d0 = ref_->rdm1_mat(target_);
      auto fock  = focksub(d0, coeff_->slice(0, ref_->nocc()));
      auto fock1 = focksub(make_shared<Matrix>(*d1_ + *d0->resize(nmo,nmo)), *coeff_);
      *fock1 -= *fock * (1.0+smith->wf1norm()); // g[d^(2)]

      shared_ptr<const Dvec> deriv = ref_->rdm1deriv(target_);
      assert(deriv->ij() == nact*nact);

      for (int i = 0; i != nact; ++i)
        for (int j = 0; j != nact; ++j)
          cideriv_->ax_plus_y(2.0*fock1->element(j,i), deriv->data(j+i*nact));
    }

    d2_ = smith->dm2();
    energy_ = smith->algo()->energy() + ref_energy_[target_];

    timer.tick_print("Postprocessing SMITH");
    cout << "    * CASPT2 energy:  " << setprecision(12) << setw(15) << energy_ << endl;
  }
#endif
}


template<>
shared_ptr<GradFile> GradEval<CASPT2Grad>::compute() {
#ifdef COMPILE_SMITH
  Timer timer;

  shared_ptr<const Reference> ref = task_->ref();
  shared_ptr<FCI> fci = task_->fci();

  const int nclosed = ref->nclosed();
  const int nact = ref->nact();

  // state-averaged density matrices
  shared_ptr<const RDM<1>> rdm1_av = nact ? ref->rdm1_av() : nullptr;
  shared_ptr<const RDM<2>> rdm2_av = nact ? ref->rdm2_av() : nullptr;

  // second order density matrix
  shared_ptr<const Matrix> d1 = task_->d1();
  // first order density matrices
  shared_ptr<const Matrix> d11 = task_->d11();
  shared_ptr<const Civec> cider = nact ? task_->cideriv() : nullptr;

  shared_ptr<const Matrix> coeff = task_->coeff();

  const int ncore = task_->ncore();
  const int nocc  = ref->nocc();
  const int nmobasis = coeff->mdim();

  // d0 including core
  shared_ptr<const Matrix> d0;
  if (nact) {
    d0 = ref->rdm1_mat(task_->target())->resize(nmobasis,nmobasis);
  } else {
    auto tmp = make_shared<Matrix>(nmobasis,nmobasis);
    for (int i = 0; i != nclosed; ++i) tmp->element(i,i) = 2.0;
    d0 = tmp;
  }
  const MatView ocoeff = coeff->slice(0, nocc);

  {
    auto dtotao = make_shared<Matrix>(*coeff * (*d0 + *d11 + *d1) ^ *coeff);
    Dipole dipole(geom_, dtotao, "CASPT2 unrelaxed");
    dipole.compute();
  }

  // compute Yrs
  shared_ptr<const DFHalfDist> half   = ref->geom()->df()->compute_half_transform(ocoeff);
  shared_ptr<const DFHalfDist> halfj  = half->apply_J();
  shared_ptr<const DFHalfDist> halfjj = halfj->apply_J();
  shared_ptr<Matrix> yrs;
  shared_ptr<const DFFullDist> fulld1; // (gamma| ir) D(ir,js)
  tie(yrs, fulld1) = task_->compute_Y(half, halfj, halfjj);

  timer.tick_print("Yrs evaluation");

  // solve CPCASSCF
  auto g0 = yrs;
  auto g1 = nact ? make_shared<Dvec>(cider, ref->nstate())
                 : make_shared<Dvec>(make_shared<Determinants>(), 1); // FIXME this is wrong for nstate > 1 in CASSCF
  auto grad = make_shared<PairFile<Matrix, Dvec>>(g0, g1);

  shared_ptr<const Dvec> civector;
  if (nact) {
    civector = ref->ciwfn()->civectors();
  } else {
    auto civec = make_shared<Dvec>(make_shared<Determinants>(), 1);
    civec->data(0)->element(0,0) = 1.0;
    civector = civec;
  }
  auto cp = make_shared<CPCASSCF>(grad, civector, halfj, ref, fci, ncore, coeff);
  shared_ptr<const Matrix> zmat, xmat, smallz;
  shared_ptr<const Dvec> zvec;
  tie(zmat, zvec, xmat, smallz) = cp->solve(task_->thresh());

  timer.tick_print("Z-CASSCF solution");

  // form relaxed 1RDM
  // form Zd + dZ^+
  shared_ptr<const Matrix> dsa = nact ? rdm1_av->rdm1_mat(nclosed)->resize(nmobasis, nmobasis) : d0;
  auto dm = make_shared<Matrix>(*zmat * *dsa + (*dsa ^ *zmat));

  auto dtot = make_shared<Matrix>(*d0 + *d11 + *d1 + *dm);
  if (smallz)
    dtot->add_block(1.0, 0, 0, nocc, nocc, smallz);

  // form zdensity
  shared_ptr<const RDM<1>> zrdm1;
  shared_ptr<const RDM<2>> zrdm2;
  if (nact) {
    auto detex = make_shared<Determinants>(nact, fci->nelea(), fci->neleb(), false, /*mute=*/true);
    tie(zrdm1, zrdm2) = fci->compute_rdm12_av_from_dvec(ref->ciwfn()->civectors(), zvec, detex);

    shared_ptr<Matrix> zrdm1_mat = zrdm1->rdm1_mat(nclosed, false)->resize(nmobasis, nmobasis);
    zrdm1_mat->symmetrize();
    dtot->ax_plus_y(1.0, zrdm1_mat);
  }

  // compute relaxed dipole to check
  auto dtotao = make_shared<Matrix>(*coeff * *dtot ^ *coeff);
  {
    Dipole dipole(geom_, dtotao, "CASPT2 relaxed");
    dipole.compute();
  }

  // xmat in the AO basis
  auto xmatao = make_shared<Matrix>(*coeff * *xmat ^ *coeff);

  // two-body part
  // first make occ-occ part (copy-and-paste from src/casscf/supercigrad.cc)
  shared_ptr<const DFFullDist> qij  = halfjj->compute_second_transform(ocoeff);
  shared_ptr<DFHalfDist> qri;
  {
    shared_ptr<const Matrix> ztrans = make_shared<Matrix>(*coeff * zmat->slice(0,nocc));
    if (nact) {
      RDM<2> D(*ref->rdm2(task_->target())+*zrdm2);
      RDM<1> dd(*ref->rdm1(task_->target())+*zrdm1);
      // symetrize dd (zrdm1 needs symmetrization)
      for (int i = 0; i != nact; ++i)
        for (int j = 0; j != nact; ++j)
          dd(j,i) = dd(i,j) = 0.5*(dd(j,i)+dd(i,j));
      shared_ptr<DFFullDist> qijd = qij->apply_2rdm(D, dd, nclosed, nact);

      qijd->ax_plus_y(2.0, halfjj->compute_second_transform(ztrans)->apply_2rdm(*rdm2_av, *rdm1_av, nclosed, nact));
      qri = qijd->back_transform(ocoeff);

      shared_ptr<const DFFullDist> qijd2 = qij->apply_2rdm(*rdm2_av, *rdm1_av, nclosed, nact);
      qri->ax_plus_y(2.0, qijd2->back_transform(ztrans));

    } else {
      shared_ptr<DFFullDist> qijd = qij->apply_closed_2RDM();
      qijd->ax_plus_y(2.0, halfjj->compute_second_transform(ztrans)->apply_closed_2RDM());
      qri = qijd->back_transform(ocoeff);

      shared_ptr<const DFFullDist> qijd2 = qij->apply_closed_2RDM();
      qri->ax_plus_y(2.0, qijd2->back_transform(ztrans));
    }
  }

  // computing hyperfine coupling
  if (task_->do_hyperfine()) {
    shared_ptr<const Matrix> dhfcc = task_->spin_density_unrelaxed();
    HyperFine hfcc(geom_, dhfcc, fci->det()->nspin(), "CASSCF");
    hfcc.compute();
  }

  // D1 part. 2.0 seems to come from the difference between smith and bagel (?)
  qri->ax_plus_y(2.0, fulld1->apply_J()->back_transform(coeff));

  // contributions from non-separable part
  shared_ptr<Matrix> qq  = qri->form_aux_2index(halfjj, 1.0);

  // separable part
  vector<shared_ptr<const Matrix>> da;
  vector<shared_ptr<const VectorB>> ca;

  auto separable_pair = [&,this](shared_ptr<const Matrix> d0occ, shared_ptr<const Matrix> d1bas) {
    shared_ptr<const Matrix> d0mo = make_shared<Matrix>(*d0occ ^ ocoeff);
    shared_ptr<const Matrix> d0ao = make_shared<Matrix>(ocoeff * *d0mo);
    shared_ptr<const Matrix> d1ao = make_shared<Matrix>(*coeff * *d1bas ^ *coeff);
    shared_ptr<const VectorB> cd0 = geom_->df()->compute_cd(d0ao);
    shared_ptr<const VectorB> cd1 = geom_->df()->compute_cd(d1ao);
    ca.push_back(cd0);
    da.push_back(d1ao);

    shared_ptr<DFHalfDist> sepd = halfjj->apply_density(d1ao);
    sepd->rotate_occ(d0occ);

    qri->ax_plus_y(-1.0, sepd);
    qri->add_direct_product(cd1, d0mo, 1.0);

    *qq += (*cd0 ^ *cd1) * 2.0;
    *qq += *halfjj->form_aux_2index(sepd, -1.0);
    return make_tuple(cd0, d1ao);
  };

  separable_pair(nact ? ref->rdm1_mat(task_->target()) : d0->get_submatrix(0,0,nocc,nocc), d1);

  if (ncore)
    separable_pair(smallz, dsa);

  // back transform the rest
  shared_ptr<DFDist> qrs = qri->back_transform(ocoeff);
  qrs->add_direct_product(ca, da, 1.0);

  timer.tick_print("Effective densities");

  // compute gradients
  shared_ptr<GradFile> gradient = contract_gradient(dtotao, xmatao, qrs, qq);
  gradient->print();
  timer.tick_print("Gradient integral contraction");

  // set energy
  energy_ = task_->energy();
  return gradient;
#else
  return nullptr;
#endif
}


tuple<shared_ptr<Matrix>, shared_ptr<const DFFullDist>>
  CASPT2Grad::compute_Y(shared_ptr<const DFHalfDist> half, shared_ptr<const DFHalfDist> halfj, shared_ptr<const DFHalfDist> halfjj) {
#ifdef COMPILE_SMITH
  const int nclosed = ref_->nclosed();
  const int nact = ref_->nact();
  const int nocc = ref_->nocc();
  const int nvirt = ref_->nvirt();
  const int nall = nocc + nvirt;
  const int nmobasis = coeff_->mdim();
  assert(nall == nmobasis);

  const MatView ocmat = coeff_->slice(0, nocc);

  shared_ptr<const DFFullDist> full = halfj->compute_second_transform(coeff_);
  shared_ptr<const DFFullDist> fullo = halfj->compute_second_transform(ocmat);

  // Y_rs = 2[Y1 + Y2 + Y3(ri) + Y4 + Y5(ri)]
  shared_ptr<Matrix> out = make_shared<Matrix>(nmobasis, nmobasis);
  const MatView ocoeff = coeff_->slice(0, nocc);

  {
    // 2 Y1 = h(d0 + d1 + d2) * 2
    // one-electron contributions
    const Matrix hmo(*coeff_ % *ref_->hcore() * *coeff_);
    shared_ptr<Matrix> d0;
    if (nact) {
      d0 = ref_->rdm1_mat(target_)->resize(nmobasis,nmobasis);
    } else {
      d0 = make_shared<Matrix>(nmobasis,nmobasis);
      for (int i = 0; i != nclosed; ++i)
        d0->element(i,i) = 2.0;
    }
    *out += hmo * (*d1_ + *d11_ + *d0) * 2.0;
  }

  {
    // Y2 = Y2_rs = Y2_ri + Y2_ra, so making both at once
    shared_ptr<Matrix> dkl;
    if (nact) {
      dkl = ref_->rdm1_mat(target_);
      dkl->sqrt();
      dkl->scale(1.0/sqrt(2.0));
    }
    Fock<1> fock(geom_, ref_->hcore()->clone(), nullptr, dkl ? ocoeff * *dkl : Matrix(ocoeff), /*grad*/false, /*rhf*/true);
    *out += *coeff_ % fock * *coeff_ * *d1_ * 2.0;
  }

  {
    // 2 Y3 = 2 Y3_ri*dm0_ji
    // coulomb
    auto dmrao = make_shared<Matrix>(*coeff_ * *d1_ ^ *coeff_);
    auto jop = geom_->df()->compute_Jop(dmrao);
    // exchange
    auto kopi = halfjj->compute_Kop_1occ(dmrao, -0.5)->transpose();

    auto tmp = make_shared<Matrix>(*coeff_ % (*jop * ocoeff + *kopi));
    if (nact) {
      *tmp *= *ref_->rdm1_mat(target_);
    } else {
      *tmp *= 2.0;
    }
    out->add_block(2.0, 0, 0, nmobasis, nocc, tmp);
  }

  // fullks will be reused for gradient contraction
  shared_ptr<const DFFullDist> fullks;
  {
    // 2 Y4 =  2 K^{kl}_{rt} D^{lk}_{ts} = 2 (kr|lj) D0_(lj,ki) +  2 (kr|lt) D1_(lt,ks)
    // construct stepwise, D1 part
    fullks = contract_D1(full);
    *out += *full->form_2index(fullks, 2.0);
    // D0 part
    shared_ptr<const DFFullDist> fulld = nact ? fullo->apply_2rdm(*ref_->rdm2(target_), *ref_->rdm1(target_), nclosed, nact)
                                              : fullo->apply_closed_2RDM();
    out->add_block(2.0, 0, 0, nmobasis, nocc, full->form_2index(fulld, 1.0));
  }

  {
    // 2 Y5 = 2 Y5_ri = 2 Ybar (Gyorffy)  = 2 (rs|tj) D^ij_st = 2 (rl|jk) D0_(il,jk) + 2 (rs|tj) D1_(is,jt)]
    // construct stepwise, D1 part
    shared_ptr<const DFHalfDist> dfback = fullks->apply_J()->back_transform(coeff_);
    auto y5ri_ao = ref_->geom()->df()->form_2index(dfback, 1.0);
    out->add_block(2.0, 0, 0, nmobasis, nocc, *coeff_ % *y5ri_ao);
  }

  return make_tuple(out, fullks);
#else
  return tuple<shared_ptr<Matrix>, shared_ptr<const DFFullDist>>();
#endif
}

