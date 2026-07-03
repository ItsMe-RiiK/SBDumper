#pragma once

class Process;

namespace stages
{
  bool baseline(int fd);
  bool visual_engine(const Process &proc, int fd);
  bool data_model(int fd);
  bool instance(int fd);
  bool workspace(int fd);
  bool camera(int fd);
  void player(int fd);
  void base_part(int fd);
  void humanoid(int fd);
  void model(int fd);
  void lighting(int fd);
  void mesh_part(int fd);
  void constants(int fd);
  void services_extra(int fd);
  void part_details(int fd);
  void humanoid_details(int fd);
  void sound(int fd);
  void attachment(int fd);
  void humanoid_ext(int fd);
  void datamodel_ext(int fd);
  void sky(int fd);
  void character_ext(int fd);
} // namespace stages
