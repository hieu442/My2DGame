// ============================================================
//  GAME GIẢI MÊ CUNG - C++17 + SFML 3.1.0
//  - 5 màn chơi, độ khó tăng dần (mê cung lớn hơn, tường hiện ngắn hơn)
//  - Mỗi màn tạo mê cung ngẫu nhiên (DFS backtracker)
//  - Tường bị ẩn; chạm vào tường thì tường hiện hình rồi mờ dần
//  - Đồng hồ đếm giờ mỗi màn
//  - Lưu 5 lần hoàn thành gần nhất của mỗi màn, hiển thị kỷ lục (nhanh nhất)
// ============================================================
#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <vector>

// ---------------------- Cấu hình chung ----------------------
constexpr unsigned WIN_W = 1000;
constexpr unsigned WIN_H = 720;
constexpr float AREA_X = 20.f, AREA_Y = 100.f;   // vùng chứa mê cung
constexpr float AREA_W = 960.f, AREA_H = 600.f;
constexpr int MAX_LEVEL = 5;
constexpr size_t HISTORY_SIZE = 5;               // số lần chơi gần nhất được lưu
const char* RECORD_FILE = "records.txt";

struct LevelConfig {
    int cols, rows;
    float revealTime;   // thời gian (giây) tường còn hiện sau khi chạm
};
// Màn càng cao: mê cung càng lớn, tường biến mất càng nhanh
const std::array<LevelConfig, MAX_LEVEL> LEVELS = { {
    {8, 5, 8.0f},
    {12, 8, 5.0f},
    {16, 10, 3.5f},
    {24, 15, 2.2f},
    {32, 20, 1.2f},
} };

// ---------------------- Tiện ích ----------------------
sf::String U(const std::string& s) { return sf::String::fromUtf8(s.begin(), s.end()); }

std::string fmtTime(float t) {
    int m = static_cast<int>(t) / 60;
    float s = t - m * 60.f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%05.2f", m, s);
    return buf;
}

struct Wall {
    sf::FloatRect rect;
    float timer = 0.f;        // thời gian hiện còn lại
    bool permanent = false;   // tường viền ngoài luôn hiện mờ
};

enum class State { Menu, Playing, LevelDone, AllDone };

// ---------------------- Lớp Game ----------------------
class Game {
public:
    Game()
        : window(sf::VideoMode({ WIN_W, WIN_H }), U("Mê Cung Bóng Tối")),
        rng(std::random_device{}()) {
        window.setFramerateLimit(60);
        loadFont();
        loadRecords();
    }

    void run() {
        sf::Clock clock;
        while (window.isOpen()) {
            float dt = clock.restart().asSeconds();
            handleEvents();
            update(dt);
            render();
        }
    }

private:
    sf::RenderWindow window;
    sf::Font font;
    std::mt19937 rng;

    State state = State::Menu;
    int level = 0;                       // 0..4
    std::vector<Wall> walls;
    float cell = 40.f, wallT = 4.f, radius = 10.f, speed = 200.f;
    float originX = 0.f, originY = 0.f;
    sf::Vector2f player;
    sf::Vector2f exitPos;

    float elapsed = 0.f;
    bool running = false;                // đồng hồ chạy từ lúc người chơi bắt đầu di chuyển
    float runTotal = 0.f;                // tổng thời gian của lượt chơi 5 màn
    float lastTime = 0.f;
    float prevBest = -1.f;
    bool newRecord = false;
    float pulse = 0.f;

    std::array<std::vector<float>, MAX_LEVEL> records;

    // ---------------- Font ----------------
    void loadFont() {
        const char* paths[] = {
            "font.ttf", "arial.ttf",
            "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf",
            "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" };
        for (auto p : paths)
            if (font.openFromFile(p)) return;
        std::cerr << "Khong tim thay font! Hay dat file font.ttf (vd: Arial) canh file chay.\n";
        std::exit(1);
    }

    // ---------------- Kỷ lục ----------------
    void loadRecords() {
        std::ifstream f(RECORD_FILE);
        int lv;
        size_t n;
        while (f >> lv >> n) {
            std::vector<float> v;
            for (size_t i = 0; i < n; ++i) {
                float t;
                f >> t;
                v.push_back(t);
            }
            if (lv >= 1 && lv <= MAX_LEVEL) {
                while (v.size() > HISTORY_SIZE) v.erase(v.begin());
                records[lv - 1] = v;
            }
        }
    }
    void saveRecords() {
        std::ofstream f(RECORD_FILE);
        for (int i = 0; i < MAX_LEVEL; ++i) {
            f << (i + 1) << ' ' << records[i].size();
            for (float t : records[i]) f << ' ' << t;
            f << '\n';
        }
    }
    float bestOf(int lv) const {
        if (records[lv].empty()) return -1.f;
        return *std::min_element(records[lv].begin(), records[lv].end());
    }

    // ---------------- Sinh mê cung ----------------
    void startLevel(int lv) {
        level = lv;
        const auto& cfg = LEVELS[lv];
        const int cols = cfg.cols, rows = cfg.rows;

        cell = std::floor(std::min(AREA_W / cols, AREA_H / rows));
        wallT = std::max(3.f, cell * 0.08f);
        radius = cell * 0.27f;
        speed = cell * 5.f;
        originX = AREA_X + (AREA_W - cell * cols) / 2.f;
        originY = AREA_Y + (AREA_H - cell * rows) / 2.f;

        enum { N = 1, E = 2, S = 4, W = 8 };
        std::vector<unsigned char> w(cols * rows, 15);   // 15 = đủ 4 tường
        std::vector<char> vis(cols * rows, 0);
        auto id = [&](int c, int r) { return r * cols + c; };

        // DFS backtracker (không đệ quy)
        std::vector<std::pair<int, int>> stack;
        stack.push_back({ 0, 0 });
        vis[0] = 1;
        while (!stack.empty()) {
            auto [c, r] = stack.back();
            std::vector<int> dirs;
            if (r > 0 && !vis[id(c, r - 1)]) dirs.push_back(0);
            if (c < cols - 1 && !vis[id(c + 1, r)]) dirs.push_back(1);
            if (r < rows - 1 && !vis[id(c, r + 1)]) dirs.push_back(2);
            if (c > 0 && !vis[id(c - 1, r)]) dirs.push_back(3);
            if (dirs.empty()) {
                stack.pop_back();
                continue;
            }
            int d = dirs[rng() % dirs.size()];
            int nc = c, nr = r;
            if (d == 0) { nr--; w[id(c, r)] &= ~N; w[id(nc, nr)] &= ~S; }
            if (d == 1) { nc++; w[id(c, r)] &= ~E; w[id(nc, nr)] &= ~W; }
            if (d == 2) { nr++; w[id(c, r)] &= ~S; w[id(nc, nr)] &= ~N; }
            if (d == 3) { nc--; w[id(c, r)] &= ~W; w[id(nc, nr)] &= ~E; }
            vis[id(nc, nr)] = 1;
            stack.push_back({ nc, nr });
        }

        // Dựng danh sách tường (hình chữ nhật)
        walls.clear();
        auto addH = [&](int c, int r, bool perm) {   // tường ngang tại hàng lưới r
            Wall wl;
            wl.rect = sf::FloatRect({ originX + c * cell - wallT / 2, originY + r * cell - wallT / 2 },
                { cell + wallT, wallT });
            wl.permanent = perm;
            walls.push_back(wl);
            };
        auto addV = [&](int c, int r, bool perm) {   // tường dọc tại cột lưới c
            Wall wl;
            wl.rect = sf::FloatRect({ originX + c * cell - wallT / 2, originY + r * cell - wallT / 2 },
                { wallT, cell + wallT });
            wl.permanent = perm;
            walls.push_back(wl);
            };
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                unsigned char f = w[id(c, r)];
                if (f & N) addH(c, r, r == 0);
                if (f & W) addV(c, r, c == 0);
                if (c == cols - 1 && (f & E)) addV(c + 1, r, true);
                if (r == rows - 1 && (f & S)) addH(c, r + 1, true);
            }

        player = { originX + cell / 2, originY + cell / 2 };
        exitPos = { originX + cell * (cols - 0.5f), originY + cell * (rows - 0.5f) };
        elapsed = 0.f;
        running = false;
        state = State::Playing;
    }

    // ---------------- Va chạm ----------------
    static sf::Vector2f closestPoint(const sf::FloatRect& r, sf::Vector2f p) {
        return { std::clamp(p.x, r.position.x, r.position.x + r.size.x),
                std::clamp(p.y, r.position.y, r.position.y + r.size.y) };
    }

    void resolveCollisions() {
        for (int iter = 0; iter < 3; ++iter) {
            for (auto& wl : walls) {
                sf::Vector2f cp = closestPoint(wl.rect, player);
                sf::Vector2f d = player - cp;
                float dist2 = d.x * d.x + d.y * d.y;
                if (dist2 < radius * radius && dist2 > 1e-6f) {
                    float dist = std::sqrt(dist2);
                    player += d / dist * (radius - dist);   // đẩy người chơi ra khỏi tường
                }
            }
        }
    }

    void touchWalls() {
        const float reach = radius + 3.f;
        const float show = LEVELS[level].revealTime;
        for (auto& wl : walls) {
            sf::Vector2f cp = closestPoint(wl.rect, player);
            sf::Vector2f d = player - cp;
            if (d.x * d.x + d.y * d.y < reach * reach) wl.timer = show;   // tường hiện hình
        }
    }

    // ---------------- Sự kiện ----------------
    void handleEvents() {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                using K = sf::Keyboard::Key;
                if (state == State::Menu) {
                    if (key->code == K::Enter) { runTotal = 0.f; startLevel(0); }
                    else if (key->code >= K::Num1 && key->code <= K::Num5) {
                        runTotal = 0.f;
                        startLevel(static_cast<int>(key->code) - static_cast<int>(K::Num1));
                    }
                    else if (key->code == K::Escape) window.close();
                }
                else if (state == State::Playing) {
                    if (key->code == K::Escape) state = State::Menu;
                    else if (key->code == K::R) startLevel(level);   // mê cung mới, tính giờ lại
                }
                else if (state == State::LevelDone) {
                    if (key->code == K::Enter) startLevel(level + 1);
                    else if (key->code == K::Escape) state = State::Menu;
                }
                else if (state == State::AllDone) {
                    if (key->code == K::Enter || key->code == K::Escape) state = State::Menu;
                }
            }
        }
    }

    // ---------------- Cập nhật ----------------
    void update(float dt) {
        pulse += dt;
        if (state != State::Playing) return;
        dt = std::min(dt, 0.05f);

        using K = sf::Keyboard::Key;
        sf::Vector2f dir{ 0.f, 0.f };
        if (sf::Keyboard::isKeyPressed(K::Left) || sf::Keyboard::isKeyPressed(K::A)) dir.x -= 1;
        if (sf::Keyboard::isKeyPressed(K::Right) || sf::Keyboard::isKeyPressed(K::D)) dir.x += 1;
        if (sf::Keyboard::isKeyPressed(K::Up) || sf::Keyboard::isKeyPressed(K::W)) dir.y -= 1;
        if (sf::Keyboard::isKeyPressed(K::Down) || sf::Keyboard::isKeyPressed(K::S)) dir.y += 1;

        if (dir.x != 0.f || dir.y != 0.f) {
            running = true;
            dir /= std::sqrt(dir.x * dir.x + dir.y * dir.y);
        }
        if (running) elapsed += dt;

        // Chia nhỏ bước di chuyển để không xuyên tường
        float dist = speed * dt;
        int steps = std::max(1, static_cast<int>(std::ceil(dist / (radius * 0.4f))));
        for (int i = 0; i < steps; ++i) {
            player += dir * (dist / steps);
            resolveCollisions();
        }
        touchWalls();

        for (auto& wl : walls)
            if (wl.timer > 0.f) wl.timer = std::max(0.f, wl.timer - dt);

        // Thắng màn
        sf::Vector2f d = player - exitPos;
        if (std::sqrt(d.x * d.x + d.y * d.y) < cell * 0.35f) finishLevel();
    }

    void finishLevel() {
        running = false;
        lastTime = elapsed;
        runTotal += elapsed;
        prevBest = bestOf(level);
        newRecord = (prevBest < 0.f || lastTime < prevBest);
        records[level].push_back(lastTime);
        while (records[level].size() > HISTORY_SIZE) records[level].erase(records[level].begin());
        saveRecords();
        state = (level == MAX_LEVEL - 1) ? State::AllDone : State::LevelDone;
    }

    // ---------------- Vẽ ----------------
    void text(const std::string& s, unsigned size, sf::Color col, float x, float y, int align = 0) {
        sf::Text t(font, U(s), size);
        t.setFillColor(col);
        auto b = t.getLocalBounds();
        float ox = b.position.x;
        if (align == 1) ox += b.size.x / 2.f;
        else if (align == 2) ox += b.size.x;
        t.setOrigin({ ox, 0.f });
        t.setPosition({ x, y });
        window.draw(t);
    }

    // Vẽ ngôi sao 5 cánh bằng hình học (không phụ thuộc font)
    void drawStar(float cx, float cy, float outerR, sf::Color col) {
        const float PI = 3.14159265f;
        sf::ConvexShape star;
        star.setPointCount(10);
        for (int i = 0; i < 10; ++i) {
            float r = (i % 2 == 0) ? outerR : outerR * 0.42f;
            float ang = -PI / 2.f + i * PI / 5.f;
            star.setPoint(i, { cx + r * std::cos(ang), cy + r * std::sin(ang) });
        }
        star.setFillColor(col);
        window.draw(star);
    }

    void drawMaze() {
        const float show = LEVELS[level].revealTime;
        const float fade = std::min(1.f, show);
        sf::RectangleShape rs;
        for (auto& wl : walls) {
            sf::Color col;
            if (wl.timer > 0.f) {
                float a = std::clamp(wl.timer / fade, 0.f, 1.f);
                col = sf::Color(80, 220, 255, static_cast<std::uint8_t>(255 * a));
            }
            else if (wl.permanent) {
                col = sf::Color(70, 70, 95);
            }
            else continue;
            // tường viền đang được chạm thì sáng lên
            if (wl.permanent && wl.timer > 0.f) col = sf::Color(80, 220, 255);
            rs.setPosition(wl.rect.position);
            rs.setSize(wl.rect.size);
            rs.setFillColor(col);
            window.draw(rs);
        }

        // Ô xuất phát
        sf::RectangleShape st({ cell * 0.5f, cell * 0.5f });
        st.setOrigin({ cell * 0.25f, cell * 0.25f });
        st.setPosition({ originX + cell / 2, originY + cell / 2 });
        st.setFillColor(sf::Color(255, 200, 60, 70));
        window.draw(st);

        // Ô thoát (luôn hiện, nhấp nháy)
        float a = 0.6f + 0.4f * std::sin(pulse * 4.f);
        sf::RectangleShape ex({ cell * 0.55f, cell * 0.55f });
        ex.setOrigin({ cell * 0.275f, cell * 0.275f });
        ex.setPosition(exitPos);
        ex.setFillColor(sf::Color(60, 255, 120, static_cast<std::uint8_t>(255 * a)));
        window.draw(ex);

        // Người chơi
        sf::CircleShape glow(radius * 1.6f);
        glow.setOrigin({ radius * 1.6f, radius * 1.6f });
        glow.setPosition(player);
        glow.setFillColor(sf::Color(255, 220, 80, 40));
        window.draw(glow);
        sf::CircleShape pl(radius);
        pl.setOrigin({ radius, radius });
        pl.setPosition(player);
        pl.setFillColor(sf::Color(255, 210, 60));
        window.draw(pl);
    }

    void drawHud() {
        const auto& cfg = LEVELS[level];
        text("Màn " + std::to_string(level + 1) + "/" + std::to_string(MAX_LEVEL), 30,
            sf::Color::White, 20, 14);
        text(fmtTime(elapsed), 44, sf::Color(255, 220, 80), WIN_W / 2.f, 8, 1);
        float best = bestOf(level);
        text(best < 0 ? "Kỷ lục: --" : "Kỷ lục: " + fmtTime(best), 26, sf::Color(120, 255, 160),
            WIN_W - 20.f, 18, 2);
        std::string hint = running
            ? "Mê cung " + std::to_string(cfg.cols) + "x" + std::to_string(cfg.rows) +
            "  |  R: mê cung mới  |  Esc: menu"
            : "Dùng WASD / phím mũi tên để di chuyển - đồng hồ chạy khi bạn bắt đầu đi";
        text(hint, 18, sf::Color(170, 170, 190), WIN_W / 2.f, 68, 1);
    }

    void overlay() {
        sf::RectangleShape r({ static_cast<float>(WIN_W), static_cast<float>(WIN_H) });
        r.setFillColor(sf::Color(0, 0, 0, 190));
        window.draw(r);
    }

    std::string historyStr(int lv) const {
        if (records[lv].empty()) return "chưa có";
        std::string s;
        for (size_t i = 0; i < records[lv].size(); ++i) {
            if (i) s += "  ";
            char b[16];
            std::snprintf(b, sizeof(b), "%.2f", records[lv][i]);
            s += b;
        }
        return s + " (s)";
    }

    void drawMenu() {
        text("MÊ CUNG BÓNG TỐI", 64, sf::Color(80, 220, 255), WIN_W / 2.f, 40, 1);
        text("Tìm đường đến ô xanh lục. Tường chỉ hiện ra khi bạn chạm vào!", 24,
            sf::Color::White, WIN_W / 2.f, 130, 1);
        text("Di chuyển: WASD / mũi tên", 22, sf::Color(190, 190, 210), WIN_W / 2.f, 170, 1);

        float y = 240;
        text("Màn", 22, sf::Color(255, 220, 80), 80, y);
        text("Kỷ lục", 22, sf::Color(255, 220, 80), 230, y);
        text("5 lần chơi gần nhất", 22, sf::Color(255, 220, 80), 400, y);
        y += 40;
        for (int i = 0; i < MAX_LEVEL; ++i, y += 50) {
            float b = bestOf(i);
            text(std::to_string(i + 1) + "  (" + std::to_string(LEVELS[i].cols) + "x" +
                std::to_string(LEVELS[i].rows) + ")", 22, sf::Color::White, 80, y);
            text(b < 0 ? "--" : fmtTime(b), 22, sf::Color(120, 255, 160), 230, y);
            text(historyStr(i), 20, sf::Color(190, 190, 210), 400, y + 2);
        }
        float a = 0.6f + 0.4f * std::sin(pulse * 4.f);
        text("Nhấn ENTER để bắt đầu từ màn 1   (hoặc phím 1-5 để chọn màn)", 26,
            sf::Color(255, 255, 255, static_cast<std::uint8_t>(255 * a)), WIN_W / 2.f, 600, 1);
        text("Esc: thoát", 18, sf::Color(150, 150, 170), WIN_W / 2.f, 660, 1);
    }

    void drawResult() {
        overlay();
        bool all = (state == State::AllDone);
        text(all ? "CHÚC MỪNG! BẠN ĐÃ HOÀN THÀNH CẢ 5 MÀN!" :
            "HOÀN THÀNH MÀN " + std::to_string(level + 1) + "!",
            all ? 40 : 52, sf::Color(120, 255, 160), WIN_W / 2.f, 150, 1);
        text("Thời gian: " + fmtTime(lastTime), 40, sf::Color(255, 220, 80), WIN_W / 2.f, 250, 1);
        if (newRecord) {
            text("KỶ LỤC MỚI!", 34, sf::Color(255, 120, 120), WIN_W / 2.f, 315, 1);
            drawStar(WIN_W / 2.f - 160.f, 335.f, 18.f, sf::Color(255, 210, 60));
            drawStar(WIN_W / 2.f + 160.f, 335.f, 18.f, sf::Color(255, 210, 60));
        }
        else
            text("Kỷ lục hiện tại: " + fmtTime(bestOf(level)), 26, sf::Color(190, 190, 210),
                WIN_W / 2.f, 320, 1);
        text("5 lần gần nhất: " + historyStr(level), 22, sf::Color::White, WIN_W / 2.f, 380, 1);
        if (all)
            text("Tổng thời gian 5 màn: " + fmtTime(runTotal), 30, sf::Color(80, 220, 255),
                WIN_W / 2.f, 440, 1);
        text(all ? "Nhấn ENTER để về menu" : "Nhấn ENTER để sang màn tiếp theo", 26,
            sf::Color::White, WIN_W / 2.f, 540, 1);
    }

    void render() {
        window.clear(sf::Color(14, 14, 24));
        if (state == State::Menu) {
            drawMenu();
        }
        else {
            drawMaze();
            drawHud();
            if (state != State::Playing) drawResult();
        }
        window.display();
    }
};

int main() {
    Game game;
    game.run();
    return 0;
}