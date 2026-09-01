#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "ActionTransport.h"

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "ActionTransportTests failed: %s (error=%lu)\n",
                 message, static_cast<unsigned long>(GetLastError()));
    std::exit(1);
  }
}

class TestDirectory {
 public:
  TestDirectory() {
    wchar_t temporary[MAX_PATH] = {};
    Require(GetTempPathW(MAX_PATH, temporary) != 0, "GetTempPathW");
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      wchar_t leaf[96] = {};
      std::swprintf(leaf, sizeof(leaf) / sizeof(leaf[0]),
                    L"fomds-action-%lu-%llu-%u",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    static_cast<unsigned long long>(GetTickCount64()), attempt);
      path_ = temporary;
      path_.append(leaf);
      if (CreateDirectoryW(path_.c_str(), nullptr)) return;
      Require(GetLastError() == ERROR_ALREADY_EXISTS,
              "unexpected CreateDirectoryW error");
    }
    Require(false, "could not create unique test directory");
  }

  ~TestDirectory() {
    WIN32_FIND_DATAW entry = {};
    const std::wstring pattern = path_ + L"\\*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &entry);
    if (find != INVALID_HANDLE_VALUE) {
      do {
        if (std::wcscmp(entry.cFileName, L".") == 0 ||
            std::wcscmp(entry.cFileName, L"..") == 0) {
          continue;
        }
        const std::wstring child = path_ + L"\\" + entry.cFileName;
        DeleteFileW(child.c_str());
      } while (FindNextFileW(find, &entry));
      FindClose(find);
    }
    RemoveDirectoryW(path_.c_str());
  }

  const std::wstring& path() const { return path_; }
  std::wstring command() const { return path_ + L"\\command.json"; }

 private:
  std::wstring path_;
};

fomds::BridgeActionSession ValidSession(const std::string& epoch = "save-123") {
  fomds::BridgeActionSession session;
  session.validated = true;
  session.protocol = fomds::kBridgeProtocol;
  session.schema = fomds::kBridgeSchema;
  session.epoch = epoch;
  session.state_sequence = 9;
  session.inventory_revision = 4;
  session.armor_revision = 6;
  session.hotbar_revision = 2;
  session.magic_revision = 8;
  session.inventory_size = 20;
  session.armor_size = 8;
  session.selected_hotbar_index = 7;
  session.learned_spell_count = 2;
  session.learned_spell_indexes[0] = 2;
  session.learned_spell_indexes[1] = 7;
  session.pinned_spell_index = 7;
  session.in_game = true;
  session.can_select_hotbar = true;
  session.can_swap_slots = true;
  session.can_consume_item = true;
  session.can_equip_armor = true;
  session.can_unequip_armor = true;
  session.can_pin_spell = true;
  session.can_unpin_spell = true;
  session.can_cast_pinned_spell = true;
  return session;
}

std::string ReadFile(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(file != INVALID_HANDLE_VALUE, "open output command");
  LARGE_INTEGER size = {};
  Require(GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
              size.QuadPart < 4096,
          "command size");
  std::string value(static_cast<std::size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  Require(::ReadFile(file, value.data(), static_cast<DWORD>(value.size()), &read,
                     nullptr) &&
              read == value.size(),
          "read output command");
  CloseHandle(file);
  return value;
}

void WriteSentinel(const std::wstring& path, const char* value) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(file != INVALID_HANDLE_VALUE, "create sentinel");
  const DWORD size = static_cast<DWORD>(std::strlen(value));
  DWORD written = 0;
  Require(WriteFile(file, value, size, &written, nullptr) && written == size,
          "write sentinel");
  CloseHandle(file);
}

std::uint64_t JsonInteger(const std::string& json, const std::string& key) {
  const std::string prefix = "\"" + key + "\":";
  const std::size_t begin = json.find(prefix);
  Require(begin != std::string::npos, "missing JSON integer");
  std::size_t end = begin + prefix.size();
  while (end < json.size() && json[end] >= '0' && json[end] <= '9') ++end;
  Require(end > begin + prefix.size(), "empty JSON integer");
  return std::strtoull(json.substr(begin + prefix.size(),
                                  end - begin - prefix.size()).c_str(),
                       nullptr, 10);
}

bool AnyTemporaryCommand(const std::wstring& directory) {
  WIN32_FIND_DATAW entry = {};
  const std::wstring pattern = directory + L"\\command.*.new.json";
  HANDLE find = FindFirstFileW(pattern.c_str(), &entry);
  if (find == INVALID_HANDLE_VALUE) return false;
  FindClose(find);
  return true;
}

void TestFailClosedAndStrictValidation() {
  TestDirectory directory;
  fomds::ActionTransport transport;
  Require(transport.Initialize(directory.path()), "initialize transport");
  Require(transport.QueueSelectHotbar(0, 1, 1).code ==
              fomds::SubmitCode::kNoValidSession,
          "reject action without session");

  auto session = ValidSession();
  session.protocol = "wrong";
  Require(!transport.UpdateSession(session), "reject wrong protocol");
  Require(!transport.HasValidSession(), "wrong protocol remains closed");
  session = ValidSession();
  session.schema = 2;
  Require(!transport.UpdateSession(session), "reject wrong schema");
  session = ValidSession();
  session.validated = false;
  Require(!transport.UpdateSession(session), "reject unvalidated state");
  session = ValidSession(std::string("bad\xc0\x80", 5));
  Require(!transport.UpdateSession(session), "reject invalid UTF-8 epoch");
  session = ValidSession();
  session.learned_spell_indexes[1] = 2;
  Require(!transport.UpdateSession(session), "reject duplicate learned index");

  session = ValidSession();
  Require(transport.UpdateSession(session), "accept exact session");
  Require(transport.QueueSelectHotbar(-1, 4, 2).code ==
              fomds::SubmitCode::kInvalidArgument,
          "reject negative slot");
  Require(transport.QueueSelectHotbar(20, 4, 2).code ==
              fomds::SubmitCode::kInvalidArgument,
          "reject out-of-range slot");
  Require(transport.QueueSelectHotbar(1, 3, 2).code ==
              fomds::SubmitCode::kStaleView,
          "reject stale inventory view");
  Require(transport.QueueSwapSlots(2, 2, 4).code ==
              fomds::SubmitCode::kInvalidArgument,
          "reject identical swap slots");
  Require(transport.QueueEquipArmor(1, 9, "fingerprint", "", 4, 6).code ==
              fomds::SubmitCode::kInvalidArgument,
          "reject out-of-range armor slot");
  Require(transport.QueueEquipArmor(1, 2, "fingerprint", "", 4, 5).code ==
              fomds::SubmitCode::kStaleView,
          "reject stale armor view");
  Require(transport.QueueUnequipArmor(1, 2, "fingerprint", "occupied", 4, 6).code ==
              fomds::SubmitCode::kInvalidArgument,
          "unequip destination must be exactly empty");
  Require(transport.QueueConsumeItem(6, "fingerprint", 4, 2).code ==
              fomds::SubmitCode::kInvalidArgument,
          "consume requires current selected index");
  Require(transport.QueueConsumeItem(7, "fingerprint", 4, 1).code ==
              fomds::SubmitCode::kStaleView,
          "consume requires current hotbar revision");
  Require(transport.QueueConsumeItem(
              7, std::string(fomds::kMaxExpectedItemFingerprintBytes + 1, 'x'),
              4, 2).code == fomds::SubmitCode::kInvalidArgument,
          "fingerprints are bounded before queue allocation");
  Require(transport.QueueConsumeItem(7, std::string("bad\xc0\x80", 5), 4, 2).code ==
              fomds::SubmitCode::kInvalidArgument,
          "fingerprints require valid UTF-8");
  Require(transport.QueuePinSpell(1, 8).code ==
              fomds::SubmitCode::kInvalidArgument,
          "never infer learned spell from array position");
  Require(transport.QueueCastPinnedSpell(2, 8).code ==
              fomds::SubmitCode::kInvalidArgument,
          "cast requires exact current pin");
  Require(transport.QueuePinSpell(2, 7).code == fomds::SubmitCode::kStaleView,
          "spell action requires current magic revision");

  session.can_select_hotbar = false;
  Require(transport.UpdateSession(session), "update capabilities");
  Require(transport.QueueSelectHotbar(1, 4, 2).code ==
              fomds::SubmitCode::kCapabilityUnavailable,
          "honor bridge capability");
  session.can_consume_item = false;
  Require(transport.UpdateSession(session), "update consume capability");
  Require(transport.QueueConsumeItem(7, "fingerprint", 4, 2).code ==
              fomds::SubmitCode::kCapabilityUnavailable,
          "honor safe-consume capability");
}

void TestExpandedTypedActionsAndEscaping() {
  TestDirectory directory;
  fomds::ActionTransportConfig config;
  config.min_write_interval_ms = 25;
  fomds::ActionTransport transport(config);
  Require(transport.Initialize(directory.path()), "initialize expanded transport");
  auto session = ValidSession("expanded-actions");
  Require(transport.UpdateSession(session), "accept expanded session");

  const std::string source = "{\"variant\":\"quote\\\" and slash\\\\\"}";
  struct ExpectedAction {
    const char* name;
    const char* shape;
  };
  const ExpectedAction expected[] = {
      {"equip_armor", "\"inventory_index\":3,\"armor_index\":1"},
      {"unequip_armor", "\"armor_index\":1,\"inventory_index\":4"},
      {"consume_item", "\"index\":7,\"expected_item\":"},
      {"pin_spell", "\"spell\":2}"},
      {"unpin_spell", "\"spell\":7}"},
      {"cast_pinned_spell", "\"spell\":7}"},
  };
  Require(transport.QueueEquipArmor(3, 1, source, "", 4, 6).code ==
              fomds::SubmitCode::kQueued,
          "queue equip armor");
  Require(transport.QueueUnequipArmor(1, 4, source, "", 4, 6).code ==
              fomds::SubmitCode::kQueued,
          "queue unequip armor");
  Require(transport.QueueConsumeItem(7, source, 4, 2).code ==
              fomds::SubmitCode::kQueued,
          "queue consume item");
  Require(transport.QueuePinSpell(2, 8).code == fomds::SubmitCode::kQueued,
          "queue exact learned spell index");
  Require(transport.QueueUnpinSpell(7, 8).code == fomds::SubmitCode::kQueued,
          "queue exact pinned spell unpin");
  Require(transport.QueueCastPinnedSpell(7, 8).code ==
              fomds::SubmitCode::kQueued,
          "queue exact pinned spell cast");

  std::uint64_t now = 5000;
  for (const ExpectedAction& action : expected) {
    const auto written = transport.Pump(now);
    Require(written.code == fomds::PumpCode::kWritten,
            "write expanded action");
    Require(transport.Pump(now + 1).code ==
                fomds::PumpCode::kWaitingForAcknowledgement,
            "expanded action keeps one command in flight");
    const std::string json = ReadFile(directory.command());
    Require(json.find(std::string("\"action\":\"") + action.name + "\"") !=
                std::string::npos &&
                json.find(action.shape) != std::string::npos,
            "expanded action protocol shape");
    if (std::strcmp(action.name, "equip_armor") == 0) {
      Require(json.find("{\\\"variant\\\":\\\"quote") != std::string::npos &&
                  json.find("slash\\\\") != std::string::npos &&
                  json.find("\"expected_equipped_item\":\"\"") !=
                      std::string::npos,
              "fingerprints are bounded JSON-escaped opaque strings");
    }
    fomds::BridgeActionAck ack;
    ack.validated = true;
    ack.protocol = fomds::kBridgeProtocol;
    ack.schema = fomds::kBridgeSchema;
    ack.epoch = session.epoch;
    ack.state_sequence = session.state_sequence;
    ack.command_sequence = written.command_sequence;
    ack.action = action.name;
    ack.ok = true;
    ack.code = "ok";
    Require(transport.HandleAcknowledgement(ack) == fomds::AckCode::kAccepted,
            "expanded action acknowledgement");
    Require(DeleteFileW(directory.command().c_str()),
            "simulate expanded action claim");
    now += 25;
  }
  Require(transport.PendingCount() == 0 && !transport.HasOutstandingCommand(),
          "expanded queue drains exactly once per acknowledgement");
}

void TestAtomicProtocolAndAcknowledgements() {
  TestDirectory directory;
  fomds::ActionTransportConfig config;
  config.min_write_interval_ms = 25;
  fomds::ActionTransport transport(config);
  Require(transport.Initialize(directory.path()), "initialize transport");
  auto session = ValidSession("save-\"quoted\\epoch");
  session.command_sequence_floor = 8000000000000000ULL;
  Require(transport.UpdateSession(session), "accept quoted epoch");

  const auto first = transport.QueueSelectHotbar(7, 4, 2);
  const auto second = transport.QueueSwapSlots(3, 8, 4);
  Require(first.code == fomds::SubmitCode::kQueued && first.request_id != 0,
          "queue selection");
  Require(second.code == fomds::SubmitCode::kQueued &&
              second.request_id > first.request_id,
          "monotonic request IDs");

  const auto written = transport.Pump(1000);
  Require(written.code == fomds::PumpCode::kWritten &&
              written.request_id == first.request_id &&
              written.command_sequence > session.command_sequence_floor,
          "write selection");
  Require(!AnyTemporaryCommand(directory.path()), "atomic temp removed");
  const std::string first_json = ReadFile(directory.command());
  Require(first_json.find("\"schema\":1") != std::string::npos,
          "schema emitted");
  Require(first_json.find("\"epoch\":\"save-\\\"quoted\\\\epoch\"") !=
              std::string::npos,
          "epoch JSON escaped");
  Require(first_json.find("\"action\":\"select_hotbar\"") !=
              std::string::npos &&
              first_json.find("\"index\":7}") != std::string::npos,
          "selection protocol shape");
  Require(first_json.find("request_id") == std::string::npos &&
              first_json.find("nonce") == std::string::npos,
          "no non-protocol fields emitted");
  Require(JsonInteger(first_json, "sequence") == written.command_sequence,
          "reported sequence matches JSON");
  Require(transport.Pump(1025).code ==
              fomds::PumpCode::kWaitingForAcknowledgement,
          "single command in flight");

  fomds::BridgeActionAck ack;
  ack.validated = true;
  ack.protocol = fomds::kBridgeProtocol;
  ack.schema = fomds::kBridgeSchema;
  ack.epoch = session.epoch;
  ack.state_sequence = session.state_sequence;
  ack.command_sequence = written.command_sequence;
  ack.action = "swap_slots";
  ack.code = "ok";
  Require(transport.HandleAcknowledgement(ack) ==
              fomds::AckCode::kDoesNotMatch &&
              transport.HasOutstandingCommand(),
          "wrong action cannot acknowledge");
  ack.action = "select_hotbar";
  Require(transport.HandleAcknowledgement(ack) == fomds::AckCode::kAccepted,
          "matching acknowledgement accepted");
  Require(DeleteFileW(directory.command().c_str()), "simulate claimed command");
  session.state_sequence += 1;
  session.command_sequence_floor += 1000;
  Require(transport.UpdateSession(session), "raise sequence from validated ack");

  Require(transport.Pump(1010).code == fomds::PumpCode::kRateLimited,
          "write rate limited");
  const auto swapped = transport.Pump(1025);
  Require(swapped.code == fomds::PumpCode::kWritten &&
              swapped.command_sequence > session.command_sequence_floor,
          "monotonic command sequence");
  const std::string second_json = ReadFile(directory.command());
  Require(second_json.find("\"action\":\"swap_slots\"") !=
              std::string::npos &&
              second_json.find("\"first\":3,\"second\":8}") !=
                  std::string::npos,
          "swap protocol shape");
}

void TestTransportOnlyResync() {
  TestDirectory directory;
  fomds::ActionTransport transport;
  Require(transport.Initialize(directory.path()), "initialize transport");
  auto session = ValidSession("gap-epoch");
  session.can_select_hotbar = false;
  session.can_swap_slots = false;
  Require(transport.UpdateSession(session), "accept resync session");
  const auto queued = transport.QueueResync();
  const auto duplicate = transport.QueueResync();
  Require(queued.code == fomds::SubmitCode::kQueued && queued.request_id != 0 &&
              duplicate.code == fomds::SubmitCode::kQueued &&
              duplicate.request_id == queued.request_id &&
              transport.PendingCount() == 1,
          "deduplicate resync request");
  const auto written = transport.Pump(1500);
  Require(written.code == fomds::PumpCode::kWritten,
          "write transport-only resync");
  const std::string json = ReadFile(directory.command());
  Require(json.find("\"action\":\"resync\"}") != std::string::npos &&
              json.find("\"index\"") == std::string::npos &&
              json.find("\"first\"") == std::string::npos,
          "resync protocol shape has no gameplay arguments");
}

void TestNoOverwriteQueueBoundAndStaleDrop() {
  TestDirectory directory;
  fomds::ActionTransportConfig config;
  config.max_queue = 2;
  config.min_write_interval_ms = 25;
  fomds::ActionTransport transport(config);
  Require(transport.Initialize(directory.path()), "initialize transport");
  auto session = ValidSession();
  Require(transport.UpdateSession(session), "accept session");
  const auto first = transport.QueueSelectHotbar(1, 4, 2);
  const auto second = transport.QueueSwapSlots(2, 3, 4);
  Require(first.code == fomds::SubmitCode::kQueued &&
              second.code == fomds::SubmitCode::kQueued,
          "fill bounded queue");
  Require(transport.QueueSelectHotbar(4, 4, 2).code ==
              fomds::SubmitCode::kQueueFull,
          "reject queue overflow");

  WriteSentinel(directory.command(), "do-not-overwrite");
  const auto busy = transport.Pump(2000);
  Require(busy.code == fomds::PumpCode::kCommandPathBusy,
          "detect existing command");
  Require(ReadFile(directory.command()) == "do-not-overwrite",
          "never overwrite existing command");
  Require(DeleteFileW(directory.command().c_str()), "remove sentinel");

  session.state_sequence += 1;
  session.inventory_revision += 1;
  Require(transport.UpdateSession(session), "accept newer state");
  const auto dropped = transport.Pump(2025);
  Require(dropped.code == fomds::PumpCode::kDroppedStaleView &&
              dropped.request_id == first.request_id,
          "drop request invalidated by inventory revision");
  Require(transport.PendingCount() == 1, "only stale head dropped per pump");
  Require(transport.Pump(2050).code == fomds::PumpCode::kDroppedStaleView,
          "drop second stale request");
}

void TestTimeoutAndEpochChangeFailClosed() {
  TestDirectory directory;
  fomds::ActionTransportConfig config;
  config.min_write_interval_ms = 25;
  config.acknowledgement_timeout_ms = 250;
  fomds::ActionTransport transport(config);
  Require(transport.Initialize(directory.path()), "initialize transport");
  auto session = ValidSession();
  Require(transport.UpdateSession(session), "accept session");
  Require(transport.QueueSelectHotbar(1, 4, 2).code ==
              fomds::SubmitCode::kQueued,
          "queue timeout request");
  Require(transport.Pump(3000).code == fomds::PumpCode::kWritten,
          "write timeout request");
  Require(DeleteFileW(directory.command().c_str()), "simulate GML claim");
  Require(transport.Pump(3250).code ==
              fomds::PumpCode::kAcknowledgementTimedOut,
          "acknowledgement timeout");
  Require(!transport.HasValidSession() && transport.PendingCount() == 0,
          "timeout fails closed and clears queue");

  session = ValidSession("new-epoch");
  Require(transport.UpdateSession(session), "accept replacement epoch");
  Require(transport.QueueSwapSlots(1, 2, 4).code ==
              fomds::SubmitCode::kQueued,
          "queue in replacement epoch");
  session.epoch = "another-epoch";
  session.state_sequence = 1;
  Require(transport.UpdateSession(session), "accept epoch transition");
  Require(transport.PendingCount() == 0 &&
              transport.Pump(4000).code == fomds::PumpCode::kIdle,
          "epoch transition discards old request");
}

}  // namespace

int main() {
  TestFailClosedAndStrictValidation();
  TestAtomicProtocolAndAcknowledgements();
  TestExpandedTypedActionsAndEscaping();
  TestTransportOnlyResync();
  TestNoOverwriteQueueBoundAndStaleDrop();
  TestTimeoutAndEpochChangeFailClosed();
  std::puts("ActionTransportTests passed");
  return 0;
}
