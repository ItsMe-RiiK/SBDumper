#include "dumper.hpp"
#include "process.hpp"
#include "stages.hpp"
#include "writer.hpp"
#include <chrono>
#include <iostream>

int main()
{
  std::cout << "==============================\n";
  std::cout << "    RIIK Dumper V1.0.0\n";
  std::cout << "==============================\n\n";

  Process proc;
  if (!proc.attach("Main"))
  {
    return 1;
  }

  int mem_fd = proc.get_mem_file();
  if (mem_fd < 0)
  {
    std::cerr << "Failed to get memory file descriptor.\n";
    return 1;
  }

  auto start_time = std::chrono::steady_clock::now();

  if (!stages::baseline(mem_fd))
    return 1;
  if (!stages::visual_engine(proc, mem_fd))
    return 1;
  if (!stages::data_model(mem_fd))
    return 1;
  if (!stages::instance(mem_fd))
    return 1;
  if (!stages::workspace(mem_fd))
    return 1;
  if (!stages::camera(mem_fd))
    return 1;

  stages::player(mem_fd);
  stages::base_part(mem_fd);
  stages::humanoid(mem_fd);
  stages::model(mem_fd);
  stages::lighting(mem_fd);
  stages::mesh_part(mem_fd);
  stages::constants(mem_fd);
  stages::services_extra(mem_fd);
  stages::part_details(mem_fd);
  stages::humanoid_details(mem_fd);
  stages::sound(mem_fd);
  stages::attachment(mem_fd);
  stages::humanoid_ext(mem_fd);
  stages::datamodel_ext(mem_fd);
  stages::sky(mem_fd);
  stages::character_ext(mem_fd);

  auto end_time = std::chrono::steady_clock::now();
  uint64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  writer::write_offsets(G_DUMPER.get_all_offsets(), elapsed_ms);

  return 0;
}
