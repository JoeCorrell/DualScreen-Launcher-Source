#pragma once
#include <utility>
#include "Protocol.h"

namespace isaacds {
// J460 resources/scripts/enums.lua and minimap2.anm2 use shape - 1.
inline std::pair<int, int> MapRoomSize(int shape) {
  if (shape >= 1 && shape <= 3) return {1, 1};
  if (shape == 4 || shape == 5) return {1, 2};
  if (shape == 6 || shape == 7) return {2, 1};
  if (shape >= 8 && shape <= 12) return {2, 2};
  return {0, 0};
}
inline int MapRoomX(const Room& room) {
  return room.grid % 13 - (room.shape == 9 ? 1 : 0);
}
inline bool MapRoomVisited(const Room& room) {
  return room.flags >= 0 ? (room.flags & 1) != 0 : room.visited != 0;
}
inline bool MapRoomBody(const Room& room, bool current) {
  return current || ((room.display_flags & 1) && (MapRoomVisited(room) ||
      (room.type != 7 && room.type != 8 && room.type != 29)));
}
inline bool MapRoomLocked(const Room& room) {
  if (!(room.display_flags & 2) || (room.display_flags & 4) || MapRoomVisited(room)) return false;
  switch (room.type) {
    case 2: case 9: case 12: case 18: case 19: case 20: case 21: case 24: return true;
    default: return false;
  }
}
inline bool MapRoomVisible(const Room& room) {
  return room.grid >= 0 && room.grid < 169 &&
         room.shape >= 1 && room.shape <= 12 && room.display_flags != 0;
}
}
