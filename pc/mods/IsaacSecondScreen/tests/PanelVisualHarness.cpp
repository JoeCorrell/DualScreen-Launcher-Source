#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Panel.h"
#include "Log.h"

namespace {

bool Send(const std::string& packet, unsigned short port = 7791) {
  SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sender == INVALID_SOCKET) return false;
  sockaddr_in target = {};
  target.sin_family = AF_INET;
  target.sin_port = htons(port);
  target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const int result = sendto(sender, packet.data(),
                            static_cast<int>(packet.size()), 0,
                            reinterpret_cast<sockaddr*>(&target),
                            sizeof(target));
  closesocket(sender);
  return result == static_cast<int>(packet.size());
}

bool Capture(HWND window, const char* path) {
  RECT client = {};
  if (!window || !GetClientRect(window, &client)) return false;
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  HDC source = GetDC(window);
  HDC memory = CreateCompatibleDC(source);
  HBITMAP bitmap = CreateCompatibleBitmap(source, width, height);
  HGDIOBJ previous = SelectObject(memory, bitmap);
  SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory),
               PRF_CLIENT);
  const bool copied = true;
  SelectObject(memory, previous);

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  std::vector<unsigned char> pixels(
      static_cast<size_t>(width) * height * 4);
  const bool read = GetDIBits(memory, bitmap, 0, height, pixels.data(), &info,
                              DIB_RGB_COLORS) == height;

  BITMAPFILEHEADER file_header = {};
  file_header.bfType = 0x4D42;
  file_header.bfOffBits = sizeof(file_header) + sizeof(BITMAPINFOHEADER);
  file_header.bfSize = file_header.bfOffBits +
                       static_cast<DWORD>(pixels.size());
  std::ofstream output(path, std::ios::binary);
  if (copied && read && output) {
    output.write(reinterpret_cast<const char*>(&file_header),
                 sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader),
                 sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
  }
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(window, source);
  return copied && read && output.good();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: PanelVisualHarness <output-prefix>\n";
    return 2;
  }
  isaacds::InitializeLog(GetModuleHandleW(nullptr));
  if (!isaacds::StartPanel(GetModuleHandleW(nullptr), "visual harness"))
    return 3;
  Sleep(6000);
  WSADATA winsock = {};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 4;
  bool sent =
      Send("DSI1\tS\t1\t120\t1\t0\tIsaac\t12\t5\t3\t6\t8\t4\t0\t1\t0\t0\t0\t0\t3.50\t4.20\t6.50\t1.10\t1.20\t2.00") &&
      Send("DSI1\tM\t2\t120\tBasement%20I\t1\t0\t0\t42\t12345\t8\t28,28,5,1,1,1,0;29,29,5,1,2,1,0;30,30,5,1,4,1,0;41,41,5,1,1,1,0;42,42,5,1,1,1,0;43,43,5,0,7,1,0;55,55,5,1,5,1,0;56,56,5,0,10,1,0") &&
      Send("DSI1\tI\t3\t120\t3\t1,1,The%20Sad%20Onion,Tears%20up,3;118,1,Brimstone,Blood%20laser%20barrage,4;105,1,D6,Reroll%20your%20destiny,4\t2\t2\t105,0,3,1;118,1,2,0\t2\t2\t10,0,1;53,1,0\t2\t2\tC,1,0,0,The%20Fool,00_TheFool;P,5,12,1,Pretty%20Fly,") &&
      Send("DSI1\tR\t4\t120\tABCD%20EFGH\tBasement%20I\t1\t0\t0\t3600\t0.25\t0.50\t0.15\t1\t2\t0,8");
  Sleep(800);
  HWND window = FindWindowW(L"IsaacSecondScreenPanel",
                            L"The Binding of Isaac: Repentance+ Dual-Screen");
  const std::string prefix = argv[1];
  bool captured = Capture(window, (prefix + "-dual.bmp").c_str());
  sent = Send("down 0.85 0.50", 7788) &&
         Send("move 0.50 0.50", 7788) &&
         Send("up 0.15 0.50", 7788) && sent;
  Sleep(400);
  captured = Capture(window, (prefix + "-stats.bmp").c_str()) && captured;
  sent = Send("down 0.85 0.50", 7788) &&
         Send("move 0.50 0.50", 7788) &&
         Send("up 0.15 0.50", 7788) && sent;
  Sleep(400);
  captured = Capture(window, (prefix + "-run-info.bmp").c_str()) && captured;
  sent = Send("down 0.85 0.50", 7788) &&
         Send("move 0.50 0.50", 7788) &&
         Send("up 0.15 0.50", 7788) && sent;
  Sleep(300);
  sent = Send("DSI1\tI\t5\t120\t2\t1,1,The%20Sad%20Onion,Tears%20up,3;105,1,D6,Reroll%20your%20destiny,4\t1\t1\t105,0,3,1\t1\t1\t10,0,1\t1\t1\tC,1,0,0,The%20Fool,00_TheFool") &&
         sent;
  Sleep(300);
  captured = Capture(window, (prefix + "-single.bmp").c_str()) && captured;
  isaacds::StopPanel();
  isaacds::ShutdownLog();
  WSACleanup();
  if (!sent || !captured) return 5;
  std::cout << "PanelVisualHarness passed\n";
  return 0;
}
