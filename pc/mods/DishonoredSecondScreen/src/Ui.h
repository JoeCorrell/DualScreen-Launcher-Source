#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Mailbox.h"
#include "Touch.h"
#include "MapObjectives.h"
#include "../../DeadIslandSecondScreen/src/Canvas.h"
#include <memory>
#include <string>
#include <vector>

namespace dishonored {

// Like Skyrim, use the existing CPU/GDI canvas on a no-activate lower window.
// The UI receives immutable snapshots and can only enqueue semantic commands.
class Ui {
public:
    explicit Ui(Bridge& bridge);
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    bool Create(const std::wstring& assetDirectory);
    bool RegisterImage(uint64_t id, const std::wstring& verifiedPath);
    // Panel pump order: Update -> Render -> Input -> Render if dirty.
    void Update(const bridge::Snapshot& snapshot, uint64_t now);
    bool Input(const Touch& event, uint64_t now);
    bool Animate(uint64_t now);
    bool Render(uint64_t now);
    void Cancel() noexcept;
    const dis::Canvas& Canvas() const noexcept { return transitioning_ ? composed_ : canvas_; }
    const std::wstring& Failure() const noexcept { return failure_; }
private:
    friend struct UiTestAccess;
    struct Resources;
    enum class HitKind : uint8_t { Entry, Action, CloseDetails, ZoomIn, ZoomOut, Recenter, Marker, JournalFilter, EquipmentFilter, StatisticsFilter, MapLegend, MapLegendSurface, MapSheets, MapSheet };
    struct Hit { dis::Box box{}; HitKind kind{}; bridge::Key key{}; uint32_t value{}; bool enabled{true}; };
    struct Row {
        bridge::Key key{};
        const bridge::Label* label{};
        std::wstring annotation;
        bool selectedInGame{}, actionable{};
        bridge::CommandKind action{};
        bool child{};
    };
    struct Scroll { float offset{}, maximum{}, velocity{}; };
    void Frame(uint64_t now);
    void Map();
    void MapLegend();
    void MapSheets();
    void Cards(unsigned columns, int top, int height);
    void Documents();
    void InventoryFilters();
    void EquippedCharms();
    void Statistics();
    void Details(const Row& row, dis::Box box, bool document = false);
    void BuildRows();
    void Label(dis::Box box, std::wstring_view text, int size = 28, bool title = false,
        dis::Color color = dis::Color(230, 224, 206), bool wrap = false, dis::Align align = dis::Align::kLeft);
    std::wstring_view Text(bridge::TextRef ref) const noexcept;
    bool Image(uint64_t id, dis::Box box, float opacity = 1.0f);
    void Border(dis::Box box, bool selected = false, uint64_t variant = 0);
    void Button(dis::Box box, std::wstring_view text, HitKind kind, bridge::Key key = 0,
        uint32_t value = 0, bool enabled = true);
    int HitAt(float x, float y) const noexcept;
    bool IsPressed(HitKind kind, bridge::Key key, uint32_t value = 0) const noexcept;
    void Activate(const Hit& hit, uint64_t now, bool detailsOnly = false);
    void Submit(bridge::CommandKind kind, bridge::Key key, uint64_t now);
    void Zoom(float factor, float anchorX, float anchorY);
    void ClampMap();
    void ChangePage(bridge::Page page, uint64_t now = 0);
    bool ComposeTransition(uint64_t now);
    void ScrollIndicator(dis::Box box, const Scroll& scroll);
    const bridge::Domain& Domain() const noexcept;
    Bridge& bridge_;
    dis::Canvas canvas_;
    dis::Canvas previousPage_, composed_, backdrop_;
    bool backdropReady_{};
    uint64_t visibleState_{};
    bool transitioning_{};
    int transitionDirection_{1}, gestureAxis_{};
    uint64_t transitionStarted_{};
    std::unique_ptr<Resources> resources_;
    const bridge::Snapshot* snapshot_{};
    bridge::Page page_{bridge::Page::Map};
    std::array<bridge::Key, 7> selected_{};
    std::array<Scroll, 7> listScroll_{}, detailScroll_{};
    std::vector<Row> rows_;
    std::vector<Hit> hits_;
    dis::Box listBox_{}, detailBox_{};
    uint32_t journalFilter_{};
    uint32_t equipmentFilter_{};
    bool savedStatistics_{};
    bool mapLegendOpen_{};
    bool mapSheetsOpen_{};
    Bridge::Bootstrap bootstrap_{Bridge::Bootstrap::Starting};
    bool dirty_{true}, pressed_{}, moved_{}, pinching_{}, fresh_{};
    Hit pressedTarget_{};
    bool hasPressedTarget_{};
    int dragRegion_{};
    float downX_{}, downY_{}, lastX_{}, lastY_{}, mapScale_{1}, mapX_{}, mapY_{};
    float mapWidth_{1192}, mapHeight_{656};
    uint64_t generation_{}, sequence_{}, lastInput_{}, lastAnimation_{}, lastTap_{}, nextRequest_{}, pendingRequest_{};
    uint64_t frameAt_{}, nextTargetExpiry_{};
    uint64_t pendingSince_{};
    bridge::Key mapKey_{};
    bridge::Key preferredMap_{}; // Local view preference, never a gameplay command.
    bridge::Map mapView_{}; // Pointer-free view of the current snapshot's acquired sheet.
    MapObjectiveProjectionCache mapObjectiveCache_{};
    bridge::CommandKind pendingKind_{};
    std::wstring feedback_;
    std::wstring failure_;
    uint64_t feedbackUntil_{};
};

} // namespace dishonored
