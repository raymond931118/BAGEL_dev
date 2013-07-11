
// BAGEL - Parallel electron correlation program.
// Filename: zmofile.h
// Copyright (C) 2013 Michael Caldwell
//
// Author: Michael Caldwell  <caldwell@u.northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and\/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
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



#ifndef __BAGEL_ZFCI_ZMOFILE_H
#define __BAGEL_ZFCI_ZMOFILE_H

#include <src/wfn/reference.h>
#include <src/scf/scf.h>
#include <src/math/zmatrix.h>

namespace bagel {

class ZMOFile {

  protected:
    int nocc_;
    int nbasis_;

    bool do_df_;
    bool hz_; // If true, do hz stuff. This may be revisited if more algorithms are implemented
    double core_energy_;

    const std::shared_ptr<const Geometry> geom_;
    const std::shared_ptr<const Reference> ref_;
    size_t sizeij_;
#if 0
    long filesize_;
    std::string filename_;
    std::vector<std::shared_ptr<Shell>> basis_;
    std::vector<int> offset_;
#endif
    std::shared_ptr<Matrix> core_fock_;
    std::unique_ptr<std::complex<double>[]> mo1e_;
    std::unique_ptr<std::complex<double>[]> mo2e_;
    std::shared_ptr<DFHalfDist> mo2e_1ext_;

    size_t mo2e_1ext_size_;
    int address_(int i, int j) const {
      assert(i <= j);
      return i+((j*(j+1))>>1);
    };
    // creates integral files and returns the core energy.
    double create_Jiiii(const int, const int);
    // this sets mo1e_, core_fock_ and returns a core energy
    virtual std::tuple<std::shared_ptr<const ZMatrix>, double> compute_mo1e(const int, const int) = 0;
    // this sets mo2e_1ext_ (half transformed DF integrals) and returns mo2e IN UNCOMPRESSED FORMAT
    virtual std::unique_ptr<std::complex<double>[]> compute_mo2e(const int, const int) = 0;
    void compress(std::shared_ptr<const ZMatrix> buf1e, std::unique_ptr<std::complex<double>[]>& buf2e);
    std::shared_ptr<const Coeff> coeff_;
  public:
    ZMOFile(const std::shared_ptr<const Reference>, const int nstart, const int nfence, const std::string method = std::string("KH"));
    ZMOFile(const std::shared_ptr<const Reference>, const int nstart, const int nfence, const std::shared_ptr<const Coeff>, const std::string method = std::string("KH"));
    ~ZMOFile();

    const std::shared_ptr<const Geometry> geom() const { return geom_; };

    int sizeij() const { return sizeij_; };
    std::complex<double> mo1e(const size_t i) const { return mo1e_[i]; };
    std::complex<double> mo2e(const size_t i, const size_t j) const { return mo2e_[i+j*sizeij_]; };
#if 0
    // This is really ugly but will work until I can think of some elegant solution that keeps mo2e(i,j,k,l) inline but doesn't require more derived classes
    // strictly i <= j, k <= l
    double mo2e_kh(const int i, const int j, const int k, const int l) const { return mo2e(address_(i,j), address_(k,l)); };
#endif
    // This is in <ij|kl> == (ik|jl) format
    std::complex<double> mo2e_hz(const int i, const int j, const int k, const int l) const { return mo2e_[l + nocc_*k + nocc_*nocc_*j + nocc_*nocc_*nocc_*i]; };
    std::complex<double> mo1e(const int i, const int j) const { return mo1e(address_(i,j)); };
    std::shared_ptr<const Matrix> core_fock() const { return core_fock_; };
    double* core_fock_ptr() { return core_fock_->data(); };
    std::complex<double>* mo1e_ptr() { return mo1e_.get(); };
    std::complex<double>* mo2e_ptr() { return mo2e_.get(); };
    const double* core_fock_ptr() const { return core_fock_->data(); };
    const std::complex<double>* mo1e_ptr() const { return mo1e_.get(); };
    const std::complex<double>* mo2e_ptr() const { return mo2e_.get(); };

    double core_energy() const { return core_energy_; };
#if 0
    std::shared_ptr<DFHalfDist> mo2e_1ext() { return mo2e_1ext_; };
    std::shared_ptr<const DFHalfDist> mo2e_1ext() const { return mo2e_1ext_; };
    void update_1ext_ints(const std::shared_ptr<const Matrix>& coeff);
#endif
};

class ZJop : public ZMOFile {
  protected:
    std::tuple<std::shared_ptr<const ZMatrix>, double> compute_mo1e(const int, const int) override;
    std::unique_ptr<std::complex<double>[]> compute_mo2e(const int, const int) override;
  public:
    ZJop(const std::shared_ptr<const Reference> b, const int c, const int d, const std::string e = std::string("KH"))
      : ZMOFile(b,c,d,e) { core_energy_ = create_Jiiii(c, d); assert(false); };
    ZJop(const std::shared_ptr<const Reference> b, const int c, const int d, std::shared_ptr<const Coeff> e, const std::string f = std::string("KH"))
      : ZMOFile(b,c,d,e,f) { core_energy_ = create_Jiiii(c, d); };
    ~ZJop() {};
};
class ZHtilde : public ZMOFile {
  protected:
    // temp storage
    std::shared_ptr<const ZMatrix> h1_tmp_;
    std::unique_ptr<std::complex<double>[]> h2_tmp_;

    std::tuple<std::shared_ptr<const ZMatrix>, double> compute_mo1e(const int, const int) override { return std::make_tuple(h1_tmp_, 0.0); };
    std::unique_ptr<std::complex<double>[]> compute_mo2e(const int, const int) override { return std::move(h2_tmp_); };

  public:
    ZHtilde(const std::shared_ptr<const Reference> b, const int c, const int d, std::shared_ptr<const ZMatrix> h1, std::unique_ptr<std::complex<double>[]> h2)
      : ZMOFile(b,c,d), h1_tmp_(h1), h2_tmp_(std::move(h2)) {
      core_energy_ = create_Jiiii(c, d);
    }
    ~ZHtilde() {};
};

}

#endif