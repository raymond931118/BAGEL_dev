//
// BAGEL - Parallel electron correlation program.
// Filename: fmm.cc
// Copyright (C) 2016 Toru Shiozaki
//
// Author: Hai-Anh Le <anh@u.northwestern.edu>
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


#include <src/periodic/fmm.h>
#include <src/util/taskqueue.h>
#include <src/util/parallel/mpi_interface.h>
#include <src/integral/rys/eribatch.h>

using namespace bagel;
using namespace std;

static const double pisq__ = pi__ * pi__;

FMM::FMM(shared_ptr<const Geometry> geom, const int ns, const int lmax, const double ws,
         const bool ex, const int lmax_k, const bool cadfj, const bool print, const int batchsize)
 : geom_(geom), ns_(ns), lmax_(lmax), ws_(ws), do_exchange_(ex), lmax_k_(lmax_k),
   do_cadfj_(cadfj), debug_(print), xbatchsize_(batchsize) {

  if (batchsize < 0)
    xbatchsize_ = (int) ceil(0.5*geom_->nele()/mpi__->size());

  if (do_cadfj_ && geom_->aux_atoms().empty())
    throw logic_error("Aux functions required for FMM-CADF-J");

  init();
}


void FMM::init() {

  centre_ = geom_->charge_center();
  nbasis_ = geom_->nbasis();
  const int ns2 = pow(2, ns_);

  const int nsp = geom_->nshellpair();
  double rad = 0;
  int isp = 0;
  isp_.reserve(nsp);
  for (int i = 0; i != nsp; ++i) {
    if (geom_->shellpair(i)->schwarz() < geom_->schwarz_thresh()) continue;
    isp_.push_back(i);
    ++isp;
    for (int j = 0; j != 3; ++j) {
      const double jco = geom_->shellpair(i)->centre(j);
      rad = max(rad, abs(jco));
    }
  }
  nsp_ = isp;
  assert(isp_.size() == nsp_);
  coordinates_.resize(nsp_);
  for (int i = 0; i != nsp_; ++i)
    coordinates_[i] = geom_->shellpair(isp_[i])->centre();

  boxsize_  = 2.05 * rad;
  unitsize_ = boxsize_/ns2;

  get_boxes();

  do_ff_ = false;
  for (int i = 0; i != nbranch_[0]; ++i)
    if (box_[i]->ninter() != 0) do_ff_ = true;
}


void FMM::get_boxes() {

  Timer fmminit;

  const int ns2 = pow(2, ns_);

  // find out unempty leaves
  vector<array<int, 3>> boxid; // max dimension 2**(ns+1)-1
  boxid.reserve(nsp_);
  unique_ptr<int[]> ibox(new int[nsp_]);

  map<array<int, 3>, int> treemap;
  assert(treemap.empty());
  int nleaf = 0;
  for (int isp = 0; isp != nsp_; ++isp) {
    array<int, 3> idxbox;
    for (int i = 0; i != 3; ++i) {
      const double coi = coordinates_[isp][i]-centre_[i];
      idxbox[i] = (int) floor(coi/unitsize_) + ns2/2 + 1;
      assert(idxbox[i] <= ns2 && idxbox[i] > 0);
    }

    map<array<int, 3>,int>::iterator box = treemap.find(idxbox);
    const bool box_found = (box != treemap.end());
    if (box_found) {
      ibox[isp] = treemap.find(idxbox)->second;
    } else {
      treemap.insert(treemap.end(), pair<array<int, 3>,int>(idxbox, nleaf));
      ibox[isp] = nleaf;
      boxid.resize(nleaf+1);
      boxid[nleaf] = idxbox;
      ++nleaf;
    }
  }
  assert(nleaf == boxid.size() && nleaf <= nsp_);

  // get leaves
  nleaf_ = nleaf;
  vector<vector<int>> leaves(nleaf);
  for (int isp = 0; isp != nsp_; ++isp) {
    const int n = ibox[isp];
    assert(n < nleaf);
    const int ingeom = isp_[isp];
    leaves[n].insert(leaves[n].end(), ingeom);
  }

  // get all unempty boxes
  int nbox = 0;
  for (int il = 0; il != nleaf; ++il) {
    vector<shared_ptr<const ShellPair>> sp(leaves[il].size());
    int cnt = 0;
    for (auto& isp : leaves[il])
      sp[cnt++] = geom_->shellpair(isp);
    array<int, 3> id = boxid[il];
    array<double, 3> centre;
    for (int i = 0; i != 3; ++i)
      centre[i] = (id[i]-ns2/2-1)*unitsize_ + 0.5*unitsize_;
   
    vector<vector<shared_ptr<const ShellPair>>> auxsp;
    map<int, int> atmap;
    if (do_cadfj_) { //if CADF-J, form list of aux shell pairs in each leaf
      vector<int> atoms_in_leaf;
      assert(atmap.empty());
      int nat = 0;
      for (auto& p : sp) {
        const int iat0 = p->atom_ind(0);
        map<int, int>::iterator at0 = atmap.find(iat0);
        const bool at0_found = (at0 != atmap.end());
        if (!at0_found) {
          atmap.insert(atmap.end(), pair<int, int>(iat0, nat));
          atoms_in_leaf.resize(nat+1);
          atoms_in_leaf[nat] = iat0;
          ++nat;
        }
        const int iat1 = p->atom_ind(1);
        map<int, int>::iterator at1 = atmap.find(iat1);
        const bool at1_found = (at1 != atmap.end());
        if (!at1_found) {
          atmap.insert(atmap.end(), pair<int, int>(iat1, nat));
          atoms_in_leaf.resize(nat+1);
          atoms_in_leaf[nat] = iat1;
          ++nat;
        }
      } 
      assert(atoms_in_leaf.size() == nat);
      vector<shared_ptr<const ShellPair>> auxsp(atoms_in_leaf.size());
      auto ashell = std::make_shared<const Shell>(sp.front()->shell(0)->spherical());
      for (auto& a : atoms_in_leaf) {
        shared_ptr<const Atom> auxat = geom_->aux_atoms()[a];
        const vector<int> tmpoff = geom_->aux_offset(a);
        vector<shared_ptr<const ShellPair>> tmpshell(auxat->shells().size());
        int ish = 0;
        for (auto& auxsh : auxat->shells()) {
          tmpshell[ish] = make_shared<const ShellPair>(array<shared_ptr<const Shell>, 2>{{auxsh, ashell}},
                                                       array<int, 2>{{tmpoff[ish], -1}}, make_pair(-1/*unknown*/, -1), make_pair(a, -1), "sierka");
          ++ish;
        }
      }
    } // end of CADF-J part
    auto newbox = make_shared<Box>(0, unitsize_, centre, il, id, lmax_, lmax_k_, sp, geom_->schwarz_thresh(), auxsp, atmap);
    box_.insert(box_.end(), newbox);
    ++nbox;
  }

  int icntc = 0;
  int icntp = ns2;
  nbranch_.resize(ns_+1);

  for (int nss = ns_; nss > -1; --nss) {
    int nbranch = 0;
    const int nss2 = pow(2, nss);
    //const int offset = pow(2, nss-2);

    for (int i = 1; i != nss2+1; ++i) {
      for (int j = 1; j != nss2+1; ++j) {
        for (int k = 1; k != nss2+1; ++k) {
          vector<shared_ptr<const ShellPair>> sp;
          array<int, 3> idxp;
          idxp[0] = (int) floor(0.5*(i+1)) + icntp;
          idxp[1] = (int) floor(0.5*(j+1)) + icntp;
          idxp[2] = (int) floor(0.5*(k+1)) + icntp;

          array<int, 3> idxc = {{i+icntc, j+icntc, k+icntc}};
          map<array<int, 3>,int>::iterator child = treemap.find(idxc);
          const bool child_found = (child != treemap.end());

          if (child_found) {
            const int ichild = treemap.find(idxc)->second;
            map<array<int, 3>,int>::iterator parent = treemap.find(idxp);
            const bool parent_found = (parent != treemap.end());

            if (!parent_found) {
              if (nss != 0) {
                const double boxsize = unitsize_ * pow(2, ns_-nss+1); 
                auto newbox = make_shared<Box>(ns_-nss+1, boxsize, array<double, 3>{{0.0, 0.0, 0.0}},
                     nbox, idxp, lmax_, lmax_k_, box_[ichild]->sp(), geom_->schwarz_thresh());
                box_.insert(box_.end(), newbox);
                treemap.insert(treemap.end(), pair<array<int, 3>,int>(idxp, nbox));
                box_[nbox]->insert_child(box_[ichild]);
                box_[ichild]->insert_parent(box_[nbox]);
                ++nbox;
              }
              ++nbranch;
            } else {
              const int ibox = treemap.find(idxp)->second;
              box_[ibox]->insert_child(box_[ichild]);
              box_[ibox]->insert_sp(box_[ichild]->sp());
              box_[ichild]->insert_parent(box_[ibox]);
              ++nbranch;
            }
          }

        }
      }
    }
    icntc = icntp;
    icntp += nss2;
    nbranch_[ns_-nss] = nbranch;
  }
  assert(accumulate(nbranch_.begin(), nbranch_.end(), 0) == nbox);
  nbox_ = nbox;

  for (auto& b : box_)
    b->init();

  int icnt = 0;
  for (int ir = ns_; ir > -1; --ir) {
    vector<shared_ptr<Box>> tmpbox(nbranch_[ir]);
    for (int ib = 0; ib != nbranch_[ir]; ++ib)
      tmpbox[ib] = box_[nbox_-icnt-nbranch_[ir]+ib];
    for (auto& b : tmpbox) {
      b->get_neigh(tmpbox, ws_);
      b->get_inter(tmpbox, ws_);
      b->sort_sp();
    }
    icnt += nbranch_[ir];
  }

  if (debug_) {
    cout << "Centre of Charge: " << setprecision(3) << geom_->charge_center()[0] << "  " << geom_->charge_center()[1] << "  " << geom_->charge_center()[2] << endl;
    cout << "ns_ = " << ns_ << " nbox = " << nbox_ << "  nleaf = " << nleaf << " nsp = " << nsp_ << " ws = " << ws_ << " lmaxJ " << lmax_;
    if (do_exchange_)
      cout << " *** BATCHSIZE " << xbatchsize_ << " lmax_k " << lmax_k_;
    cout << " boxsize = " << boxsize_ << " leafsize = " << unitsize_ << endl;
    int i = 0;
    for (auto& b : box_) {
      const bool ipar = (b->parent()) ? true : false;
      int nsp_neigh = 0;
      for (auto& n : b->neigh()) nsp_neigh += n->nsp();
      int nsp_inter = 0;
      for (auto& i : b->interaction_list()) nsp_inter += i->nsp();

      cout << i << " rank = " << b->rank() << "  boxsize = " << b->boxsize() << " extent = " << b->extent() << " nsp = " << b->nsp()
           << " nchild = " << b->nchild() << " nneigh = " << b->nneigh() << " nsp " << nsp_neigh
           << " ninter = " << b->ninter() << " nsp " << nsp_inter
           << " centre = " << b->centre(0) << " " << b->centre(1) << " " << b->centre(2)
           << " idxc = " << b->tvec()[0] << " " << b->tvec()[1] << " " << b->tvec()[2] << " *** " << ipar;
      cout << endl;
      ++i;
    }
  }

  fmminit.tick_print("FMM initialisation");
}


void FMM::M2M(shared_ptr<const Matrix> density, const bool dox) const {

  Timer m2mtime;
  for (int i = 0; i != nbranch_[0]; ++i)
    if (i % mpi__->size() == mpi__->rank())
      box_[i]->compute_M2M(density);

  for (int i = 0; i != nbranch_[0]; ++i)
    mpi__->broadcast(box_[i]->multipole()->data(), box_[i]->multipole()->size(), i % mpi__->size());

  m2mtime.tick_print("Compute multipoles");

  int icnt = nbranch_[0];
  for (int i = 1; i != ns_+1; ++i) {
    for (int j = 0; j != nbranch_[i]; ++j, ++icnt)
      box_[icnt]->compute_M2M(density);
  }
  m2mtime.tick_print("M2M pass");
  assert(icnt == nbox_);
}


void FMM::M2M_X(shared_ptr<const Matrix> ocoeff_sj, shared_ptr<const Matrix> ocoeff_ui) const {

  Timer m2mtime;

  int icnt = 0;
  for (int i = 0; i != ns_+1; ++i) {
    for (int j = 0; j != nbranch_[i]; ++j, ++icnt)
      box_[icnt]->compute_M2M_X(ocoeff_sj, ocoeff_ui);
  }
  m2mtime.tick_print("M2M-X pass");
  assert(icnt == nbox_);
}


void FMM::M2L(const bool dox) const {

  Timer m2ltime;

  TaskQueue<function<void(void)>> tasks(nbox_);
  if (!dox) {
    for (auto& b : box_)
      tasks.emplace_back(
        [this, b]() { b->compute_M2L(); }
      );
    tasks.compute();
    m2ltime.tick_print("M2L pass");
  } else {
    for (auto& b : box_)
      tasks.emplace_back(
        [this, b]() { b->compute_M2L_X(); }
      );
    tasks.compute();
    m2ltime.tick_print("M2L-X pass");
  }
}


void FMM::L2L(const bool dox) const {

  Timer l2ltime;

  int icnt = 0;
  if (!dox) {
    for (int ir = ns_; ir > -1; --ir) {
      for (int ib = 0; ib != nbranch_[ir]; ++ib)
        box_[nbox_-icnt-nbranch_[ir]+ib]->compute_L2L();

      icnt += nbranch_[ir];
    }

    l2ltime.tick_print("L2L pass");
  } else {
    for (int ir = ns_; ir > -1; --ir) {
      for (int ib = 0; ib != nbranch_[ir]; ++ib)
        box_[nbox_-icnt-nbranch_[ir]+ib]->compute_L2L_X();

      icnt += nbranch_[ir];
    }
    l2ltime.tick_print("L2L-X pass");
  }
}


shared_ptr<const Matrix> FMM::compute_Fock_FMM(shared_ptr<const Matrix> density) const {

  auto out = make_shared<Matrix>(nbasis_, nbasis_);
  out->zero();
 
  Timer fmmtime;
  M2M(density);
  M2L();
  L2L();

  Timer nftime;

  if (density) {
    assert(nbasis_ == density->ndim());
    auto maxden = make_shared<VectorB>(geom_->nshellpair());
    const double* density_data = density->data();
    for (int i01 = 0; i01 != geom_->nshellpair(); ++i01) {
      shared_ptr<const Shell> sh0 = geom_->shellpair(i01)->shell(1);
      const int offset0 = geom_->shellpair(i01)->offset(1);
      const int size0 = sh0->nbasis();

      shared_ptr<const Shell> sh1 = geom_->shellpair(i01)->shell(0);
      const int offset1 = geom_->shellpair(i01)->offset(0);
      const int size1 = sh1->nbasis();

      double denmax = 0.0;
      for (int i0 = offset0; i0 != offset0 + size0; ++i0) {
        const int i0n = i0 * density->ndim();
        for (int i1 = offset1; i1 != offset1 + size1; ++i1)
          denmax = max(denmax, fabs(density_data[i0n + i1]));
      }
      (*maxden)(i01) = denmax;
    }

    auto ff = make_shared<Matrix>(nbasis_, nbasis_);
    for (int i = 0; i != nbranch_[0]; ++i)
      if (i % mpi__->size() == mpi__->rank()) {
        auto ei = box_[i]->compute_Fock_nf(density, maxden);
        blas::ax_plus_y_n(1.0, ei->data(), nbasis_*nbasis_, out->data());
        auto ffi = box_[i]->compute_Fock_ff(density);
        blas::ax_plus_y_n(1.0, ffi->data(), nbasis_*nbasis_, ff->data());
      }
    out->allreduce();
    ff->allreduce();

    const double enj = 0.5*density->dot_product(*ff);
    cout << "    o  Far-field Coulomb energy: " << setprecision(9) << enj << endl;

    for (int i = 0; i != nbasis_; ++i) out->element(i, i) *= 2.0;
    out->fill_upper();
    *out += *ff;
  }

  nftime.tick_print("near-field");
  fmmtime.tick_print("FMM-J");

  return out;
}


shared_ptr<const Matrix> FMM::compute_K_ff_from_den(shared_ptr<const Matrix> density, shared_ptr<const Matrix> overlap) const {

  if (!do_exchange_ || !density)
    return overlap->clone();

  Timer ktime;
  shared_ptr<Matrix> coeff = density->copy();
  *coeff *= -1.0;
  int nocc = 0;
  {
    VectorB vec(density->ndim());
    coeff->diagonalize(vec);
    for (int i = 0; i != density->ndim(); ++i) {
      if (vec[i] < -1.0e-8) {
        ++nocc;
        const double fac = std::sqrt(-vec(i));
        for_each(coeff->element_ptr(0,i), coeff->element_ptr(0,i+1), [&fac](double& i) { i *= fac; });
      } else { break; }
    }
  }
  auto ocoeff = make_shared<const Matrix>(coeff->slice(0,nocc));

  auto krj = make_shared<Matrix>(nbasis_, nocc);
  const int nbatch = (nocc-1) / xbatchsize_+1;
  StaticDist dist(nocc, nbatch);
  vector<pair<size_t, size_t>> table = dist.atable();

  Timer mtime;

  int u = 0;
  for (auto& itable : table) {
    if (u++ % mpi__->size() == mpi__->rank()) {
      if (debug_) 
        cout << "BATCH " << u << "  from " << itable.first << " doing " << itable.second << " MPI " << u << endl;
      auto ocoeff_ui = make_shared<const Matrix>(ocoeff->slice(itable.first, itable.first+itable.second));
      shared_ptr<const Matrix> ocoeff_sj = ocoeff;

      M2M_X(ocoeff_sj, ocoeff_ui);
      M2L(true);
      L2L(true);
      Timer assembletime;
      for (int i = 0; i != nbranch_[0]; ++i) {
        auto ffx = box_[i]->compute_Fock_ff_K(ocoeff_ui);
        assert(ffx->ndim() == nbasis_ && ffx->mdim() == nocc);
        blas::ax_plus_y_n(1.0, ffx->data(), nbasis_*nocc, krj->data());
      }
      assembletime.tick_print("Building Krj");
    }
  }
  krj->allreduce();

  Timer projtime;
  auto kij = make_shared<const Matrix>(*ocoeff % *krj);
// check kij is symmetric
  auto kji = kij->transpose();
  const double err = (*kij - *kji).rms();
  if (err > 1e-15 && debug_)
     cout << " *** Warning: Kij is not symmetric: rms(K-K^T) = " << setprecision(20) << err << endl;

  auto sc = make_shared<const Matrix>(*overlap * *ocoeff);
  auto sck = make_shared<const Matrix>(*sc ^ *krj);
  auto krs = make_shared<const Matrix>(*sck + *(sck->transpose()) - *sc * (*kij ^ *sc));
  auto ksr = krs->transpose();
  const double errk = (*krs - *ksr).rms();
  if (errk > 1e-15 && debug_)
    cout << " *** Warning: Krs is not symmetric: rms(K-K^T) = " << setprecision(20) << errk << endl;

  projtime.tick_print("Krs from Krj");

  const double enk = 0.5*density->dot_product(*krs);
  cout << "          o    Far-field Exchange energy: " << setprecision(6) << enk << endl;

  ktime.tick_print("FMM-K");
  return krs;
}


shared_ptr<const Matrix> FMM::compute_K_ff(shared_ptr<const Matrix> ocoeff, shared_ptr<const Matrix> overlap) const {

  if (!do_exchange_ || !ocoeff)
    return overlap->clone();

  Timer ktime;
  const int nocc = ocoeff->mdim();
  auto krj = make_shared<Matrix>(nbasis_, nocc);
  const int nbatch = (nocc-1) / xbatchsize_+1;
  StaticDist dist(nocc, nbatch);
  vector<pair<size_t, size_t>> table = dist.atable();

  Timer mtime;

  int u = 0;
  for (auto& itable : table) {
    if (u++ % mpi__->size() == mpi__->rank()) {
      if (debug_)
        cout << "BATCH " << u << "  from " << itable.first << " doing " << itable.second << " MPI " << u << endl;
      auto ocoeff_ui = make_shared<const Matrix>(ocoeff->slice(itable.first, itable.first+itable.second));
      shared_ptr<const Matrix> ocoeff_sj = ocoeff;

      M2M_X(ocoeff_sj, ocoeff_ui);
      M2L(true);
      L2L(true);
      Timer assembletime;
      for (int i = 0; i != nbranch_[0]; ++i) {
        auto ffx = box_[i]->compute_Fock_ff_K(ocoeff_ui);
        assert(ffx->ndim() == nbasis_ && ffx->mdim() == nocc);
        blas::ax_plus_y_n(1.0, ffx->data(), nbasis_*nocc, krj->data());
      }
      assembletime.tick_print("Building Krj");
    }
  }
  krj->allreduce();

  if (debug_) {
    Timer projtime;
    auto kij = make_shared<const Matrix>(*ocoeff % *krj);
    // check kij is symmetric
    auto kji = kij->transpose();
    const double err = (*kij - *kji).rms();
    if (err > 1e-15 && debug_)
      cout << " *** Warning: Kij is not symmetric: rms(K-K^T) = " << setprecision(20) << err << endl;

    projtime.tick_print("Projecting K");
  }
  
  const double enk = ocoeff->dot_product(*krj);
  cout << "          o    Far-field Exchange energy: " << setprecision(9) << enk << endl;

  ktime.tick_print("FMM-K");
  return krj;
}


void FMM::print_boxes(const int i) const {

  int ib = 0;
  for (auto& b : box_) {
    if (b->rank() == i) {
      cout << "Box " << ib << " rank = " << i << " *** size " << b->boxsize() << " *** nchild = " << b->nchild() << " *** nsp = " << b->nsp() << " *** Shell pairs at:" << endl;
      for (int i = 0; i != b->nsp(); ++i)
        cout << setprecision(5) << b->sp(i)->centre(0) << "  " << b->sp(i)->centre(1) << "  " << b->sp(i)->centre(2) << endl;
      ++ib;
    }
    if (b->rank() > i) break;
  }

}


shared_ptr<const Matrix> FMM::compute_Fock_FMM_K(shared_ptr<const Matrix> density) const {

  auto out = make_shared<Matrix>(nbasis_, nbasis_);
  out->zero();
 
  Timer nftime;
  if (density) {
    assert(nbasis_ == density->ndim());
    auto maxden = make_shared<VectorB>(geom_->nshellpair());
    const double* density_data = density->data();
    for (int i01 = 0; i01 != geom_->nshellpair(); ++i01) {
      shared_ptr<const Shell> sh0 = geom_->shellpair(i01)->shell(1);
      const int offset0 = geom_->shellpair(i01)->offset(1);
      const int size0 = sh0->nbasis();

      shared_ptr<const Shell> sh1 = geom_->shellpair(i01)->shell(0);
      const int offset1 = geom_->shellpair(i01)->offset(0);
      const int size1 = sh1->nbasis();

      double denmax = 0.0;
      for (int i0 = offset0; i0 != offset0 + size0; ++i0) {
        const int i0n = i0 * density->ndim();
        for (int i1 = offset1; i1 != offset1 + size1; ++i1)
          denmax = max(denmax, fabs(density_data[i0n + i1]));
      }
      (*maxden)(i01) = denmax;
    }

    for (int i = 0; i != nbranch_[0]; ++i)
      if (i % mpi__->size() == mpi__->rank()) {
        auto ei = box_[i]->compute_Fock_nf_K(density, maxden);
        blas::ax_plus_y_n(1.0, ei->data(), nbasis_*nbasis_, out->data());
      }
    out->allreduce();

    for (int i = 0; i != nbasis_; ++i) out->element(i, i) *= 2.0;
    out->fill_upper();
  }

  nftime.tick_print("near-field-K");

  return out;
}


shared_ptr<const Matrix> FMM::compute_Fock_FMM_J(shared_ptr<const Matrix> density) const {

  auto out = make_shared<Matrix>(nbasis_, nbasis_);
  out->zero();
 
  Timer fmmtime;
  M2M(density);
  M2L();
  L2L();

  Timer nftime;

  if (density) {
    assert(nbasis_ == density->ndim());
    auto maxden = make_shared<VectorB>(geom_->nshellpair());
    const double* density_data = density->data();
    for (int i01 = 0; i01 != geom_->nshellpair(); ++i01) {
      shared_ptr<const Shell> sh0 = geom_->shellpair(i01)->shell(1);
      const int offset0 = geom_->shellpair(i01)->offset(1);
      const int size0 = sh0->nbasis();

      shared_ptr<const Shell> sh1 = geom_->shellpair(i01)->shell(0);
      const int offset1 = geom_->shellpair(i01)->offset(0);
      const int size1 = sh1->nbasis();

      double denmax = 0.0;
      for (int i0 = offset0; i0 != offset0 + size0; ++i0) {
        const int i0n = i0 * density->ndim();
        for (int i1 = offset1; i1 != offset1 + size1; ++i1)
          denmax = max(denmax, fabs(density_data[i0n + i1]));
      }
      (*maxden)(i01) = denmax;
    }

    auto ff = make_shared<Matrix>(nbasis_, nbasis_);
    for (int i = 0; i != nbranch_[0]; ++i)
      if (i % mpi__->size() == mpi__->rank()) {
        auto ei = box_[i]->compute_CADF_nf_J(density, XY_);
        //auto ei = box_[i]->compute_Fock_nf_J(density, maxden);
        blas::ax_plus_y_n(1.0, ei->data(), nbasis_*nbasis_, out->data());
        auto ffi = box_[i]->compute_Fock_ff(density);
        blas::ax_plus_y_n(1.0, ffi->data(), nbasis_*nbasis_, ff->data());
      }
    out->allreduce();
    ff->allreduce();

    const double enj = 0.5*density->dot_product(*ff);
    cout << "    o  Far-field Coulomb energy: " << setprecision(9) << enj << endl;

    for (int i = 0; i != nbasis_; ++i) out->element(i, i) *= 2.0;
    out->fill_upper();
    ff->fill_lower();

    *out += *ff;
  }

  nftime.tick_print("near-field-J");

  return out;
}


void FMM::compute_2index() {

  Timer time2index;
  vector<shared_ptr<const Shell>> ashell;
  for (int n = 0; n != geom_->aux_atoms().size(); ++n) {
    const vector<shared_ptr<const Shell>> tmpsh = geom_->aux_atoms()[n]->shells();
    ashell.insert(ashell.end(), tmpsh.begin(), tmpsh.end());
  }

  int nbas = 0; for (auto& a : ashell) nbas += a->nbasis();
  auto XY = make_shared<Matrix>(nbas, nbas);
  TaskQueue<function<void(void)>> tasks(ashell.size()*ashell.size());
  auto b3 = make_shared<const Shell>(ashell.front()->spherical());

  tasks.emplace_back(
    [this, ashell, &b3, &XY, nbas]() {
      double* const data = XY->data();
      int u = 0;
      int o0 = 0;
      for (auto& b0 : ashell) {
        int o1 = 0;
        for (auto& b1 : ashell) {
          if (o0 <= o1) {
            ERIBatch eribatch(array<shared_ptr<const Shell>,4>{{b1, b3, b0, b3}}, 2.0);
            eribatch.compute();
            const double* eridata = eribatch.data();
            for (int j0 = o0; j0 != o0 + b0->nbasis(); ++j0)
              for (int j1 = o1; j1 != o1 + b1->nbasis(); ++j1, ++eridata)
                data[j1*nbas+j0] = data[j0*nbas+j1] = *eridata;
          }
          o1 += b1->nbasis();
        }
        o0 += b0->nbasis();
      }
    }
  );

  tasks.compute();
  time2index.tick_print("2-index (X|Y)");
  XY_ = XY;
}
