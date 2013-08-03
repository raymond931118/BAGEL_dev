//
// BAGEL - Parallel electron correlation program.
// Filename: test_prop.cc
// Copyright (C) 2012 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
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

#include <sstream>
#include <src/prop/dipole.h>
#include <src/scf/scf.h>
#include <src/scf/rohf.h>
#include <src/scf/uhf.h>
#include <src/wfn/reference.h>

std::array<double,3> dipole(std::string filename) {
  auto ofs = std::make_shared<std::ofstream>(filename + "_dipole.testout", std::ios::trunc);
  std::streambuf* backup_stream = std::cout.rdbuf(ofs->rdbuf());

  // a bit ugly to hardwire an input file, but anyway...
  std::stringstream ss; ss << "../../test/" << filename << ".json";
  auto idata = std::make_shared<const PTree>(ss.str());
  auto keys = idata->get_child("bagel");
  std::shared_ptr<Geometry> geom;

  for (auto& itree : *keys) {
    std::string method = itree->get<std::string>("title", "");
    std::transform(method.begin(), method.end(), method.begin(), ::tolower);

    if (method == "molecule") {
      geom = std::make_shared<Geometry>(itree);

    } else if (method == "hf") {
      auto scf = std::make_shared<SCF>(itree, geom);
      scf->compute();
      std::shared_ptr<const Matrix> dtot = scf->coeff()->form_density_rhf(scf->nocc());

      Dipole dipole(geom, dtot);
      std::array<double,3> d = dipole.compute();
      std::cout.rdbuf(backup_stream);
      return d;
    }
  }
  assert(false);
  return std::array<double,3>();
}

static std::array<double,3> hf_svp_dfhf_dipole_ref() {
  return std::array<double,3>{{0.0, 0.0, 1.055510}};
}

using ARRAY = std::array<double,3>;

BOOST_AUTO_TEST_SUITE(TEST_PROP)

BOOST_AUTO_TEST_CASE(DIPOLE) {
    BOOST_CHECK(compare<ARRAY>(dipole("hf_svp_dfhf"),        hf_svp_dfhf_dipole_ref(), 1.0e-6));
}

BOOST_AUTO_TEST_SUITE_END()
