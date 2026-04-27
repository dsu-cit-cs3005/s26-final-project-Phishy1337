#pragma once

#include <cstddef>
#include <filesystem>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "RobotBase.h"
#include "RadarObj.h"

enum class TerrainType
{
    Empty,
    Mound,
    Pit,
    Flame
};

struct Cell
{
    TerrainType terrain = TerrainType::Empty;
    int robot_index = -1;
    int dead_robot_index = -1;
};

struct ArenaConfig
{
    int height = 20;
    int width = 20;
    int max_rounds = 1;
    double sleep_interval = 0.0;
    bool game_state_live = false;
    int flamethrowers = 0;
    int pits = 0;
    int mounds = 0;
};

struct RgbColor
{
    int red = 255;
    int green = 255;
    int blue = 255;
};

struct RobotRecord
{
    RobotBase* robot = nullptr;
    void* handle = nullptr;
    std::string source_name;
    std::string summary;
    char display_char = '?';
    bool alive = true;
    RgbColor color;
    int shots_fired = 0;
    int attacks_landed = 0;
    int damage_dealt = 0;
    int damage_taken = 0;
    int damage_blocked = 0;
    int distance_traveled = 0;
    int kills = 0;
    int blocked_moves = 0;
    int stuck_turns = 0;
    int rounds_survived = 0;
};

struct ShotResult
{
    std::string weapon_type;
    bool fired = false;
    bool hit = false;
    std::string target_label;
    std::vector<std::string> hit_details;
};

struct MoveResult
{
    int start_row = 0;
    int start_col = 0;
    int end_row = 0;
    int end_col = 0;
    bool attempted = false;
    bool stuck = false;
    bool blocked = false;
};

class Grid
{
public:
    Grid() = default;
    Grid(int rows, int cols);

    void resize(int rows, int cols);
    void clear();

    int rows() const;
    int cols() const;
    bool in_bounds(int row, int col) const;

    Cell& at(int row, int col);
    const Cell& at(int row, int col) const;

private:
    std::size_t index_for(int row, int col) const;

    int m_rows = 0;
    int m_cols = 0;
    std::vector<Cell> m_cells;
};

class Arena
{
public:
    Arena();
    ~Arena();

    bool load_config(const std::string& path);
    void initialize();
    void run();
    void print_board() const;

    const ArenaConfig& config() const;
    const Grid& grid() const;

private:
    bool load_robots();
    void cleanup_robots();
    std::vector<std::filesystem::path> discover_robot_sources() const;
    bool compile_robot_source(const std::filesystem::path& source_path,
                              const std::filesystem::path& shared_lib_path) const;
    bool load_robot_library(const std::filesystem::path& source_path,
                            const std::filesystem::path& shared_lib_path,
                            char display_char);
    void place_robots();
    void initialize_empty_board();
    void place_obstacles();
    void place_random_terrain(TerrainType terrain, int count);
    bool is_cell_empty_for_placement(int row, int col) const;
    void print_title_screen() const;
    bool has_winner(int& winner_index) const;
    int living_robot_count() const;
    std::string resolve_robot_turn(int robot_index);
    std::vector<RadarObj> scan_radar(int robot_index, int radar_direction) const;
    ShotResult resolve_shot(int robot_index, int shot_row, int shot_col);
    MoveResult resolve_move(int robot_index, int direction, int distance);
    std::string apply_damage_to_robot(int target_index, int min_damage, int max_damage,
                                      const std::string& source_text, int source_robot_index);
    std::vector<std::pair<int, int>> build_line_cells(int origin_row, int origin_col,
                                                      int target_row, int target_col,
                                                      int max_steps) const;
    std::vector<std::pair<int, int>> build_flamethrower_cells(int origin_row, int origin_col,
                                                              int target_row, int target_col) const;
    bool try_get_robot_at(int row, int col, int& robot_index, bool include_dead) const;
    char visible_cell_type(int row, int col) const;
    void print_frame_header() const;
    std::string render_cell(int row, int col) const;
    std::string format_robot_status(int robot_index, const std::string& detail) const;
    void print_round_summary(const std::vector<std::string>& turn_summaries,
                             const std::vector<std::string>& damage_events) const;
    void print_post_match_summary(int winner_index) const;
    static std::string trim(const std::string& value);
    static bool parse_bool(const std::string& value, bool& parsed_value);
    static bool is_valid_robot_filename(const std::string& filename);

    ArenaConfig m_config;
    Grid m_grid;
    int m_round = 0;
    std::vector<RobotRecord> m_robots;
    std::vector<std::string> m_round_damage_events;
    std::vector<RgbColor> m_robot_color_pool;
    std::mt19937 m_rng;
};
