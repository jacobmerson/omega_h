#include <iostream>

#include "Omega_h_cmdline.hpp"
#include "Omega_h_file.hpp"
#include "Omega_h_mesh.hpp"

int main(int argc, char** argv) {
  auto lib = Omega_h::Library(&argc, &argv);
  auto comm = lib.world();
  Omega_h::CmdLine cmdline;
  cmdline.add_arg<std::string>("mesh-in");
  cmdline.add_arg<std::string>("model-in");
  cmdline.add_arg<std::string>("mesh-out");
  if (!cmdline.parse_final(comm, &argc, argv)) return -1;
  auto mesh_in = cmdline.get<std::string>("mesh-in");
  auto model_in = cmdline.get<std::string>("model-in");
  auto mesh_out = cmdline.get<std::string>("mesh-out");
  auto mesh = Omega_h::meshsim::read(mesh_in, model_in, comm);
  Omega_h::binary::write(mesh_out, &mesh);
  std::cout << "wrote mesh " << mesh_out << "\n";
  std::cout << "Number of Edge Tags: " << mesh.ntags(1) << std::endl;  
  // Check first 10 edges of interior points for osh and sim
  unsigned int nedge = mesh.nedges();
  Omega_h::LOs edgeVerts = mesh.ask_verts_of(1);
    
  for (uint i = 0; i < nedge*2+12; i+=2) {
     std::cout << edgeVerts.get(i) << ' ' << edgeVerts.get(i+1) << std::endl;
  }
  
  
  return 0;
}
