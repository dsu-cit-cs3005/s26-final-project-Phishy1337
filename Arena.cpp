#include "Arena.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
constexpr std::size_t kMaxRobotSummaryChars = 50;
constexpr int kHammerRange = 1;
constexpr char kRobotChars[] = {'@', '#', '$', '%', '&', '!', '?', '*', '+', '=', 'A', 'B', 'C', 'D', 'E', 'F'};
constexpr RgbColor kMoundColor{181, 140, 82};
constexpr RgbColor kPitColor{92, 51, 23};
constexpr RgbColor kFlameColor{255, 140, 0};
constexpr RgbColor kDeadRobotColor{140, 140, 140};
constexpr RgbColor kRobotPalette[] = {
    {68, 170, 255},
    {255, 99, 132},
    {102, 220, 124},
    {255, 200, 87},
    {186, 104, 200},
    {255, 159, 64},
    {77, 208, 225},
    {244, 143, 177},
    {174, 213, 129},
    {129, 212, 250},
    {255, 112, 67},
    {124, 179, 66},
    {121, 134, 203},
    {255, 238, 88},
    {240, 98, 146},
    {79, 195, 247}
};

char terrain_to_char(TerrainType terrain)
{
    switch (terrain)
    {
        case TerrainType::Mound:
            return 'M';
        case TerrainType::Pit:
            return 'P';
        case TerrainType::Flame:
            return 'F';
        case TerrainType::Empty:
        default:
            return '.';
    }
}

const char* weapon_name(WeaponType weapon)
{
    switch (weapon)
    {
        case flamethrower:
            return "FLAMETHROWER";
        case railgun:
            return "RAILGUN";
        case grenade:
            return "GRENADE";
        case hammer:
            return "HAMMER";
        default:
            return "UNKNOWN";
    }
}

std::pair<int, int> perpendicular_left(int delta_row, int delta_col)
{
    return {-delta_col, delta_row};
}

std::pair<int, int> perpendicular_right(int delta_row, int delta_col)
{
    return {delta_col, -delta_row};
}

std::string color_code(const RgbColor& color)
{
    return "\033[38;2;" + std::to_string(color.red) + ';' +
           std::to_string(color.green) + ';' +
           std::to_string(color.blue) + "m";
}

std::string reset_code()
{
    return "\033[0m";
}

std::string colorize(const std::string& text, const RgbColor& color)
{
    return color_code(color) + text + reset_code();
}

std::string colorize(char ch, const RgbColor& color)
{
    return colorize(std::string(1, ch), color);
}

std::string colored_robot_label(const RobotRecord& record)
{
    return colorize(record.source_name + " " + record.display_char, record.color);
}

template <typename Metric>
int best_robot_index(const std::vector<RobotRecord>& robots, Metric metric)
{
    int best_index = -1;
    int best_value = 0;
    for (std::size_t index = 0; index < robots.size(); ++index)
    {
        const int value = metric(robots[index]);
        if (best_index == -1 || value > best_value)
        {
            best_index = static_cast<int>(index);
            best_value = value;
        }
    }
    return best_index;
}
} // namespace

Grid::Grid(int rows, int cols)
{
    resize(rows, cols);
}

void Grid::resize(int rows, int cols)
{
    if (rows < 0 || cols < 0)
    {
        throw std::invalid_argument("Grid dimensions cannot be negative.");
    }

    m_rows = rows;
    m_cols = cols;
    m_cells.assign(static_cast<std::size_t>(rows * cols), Cell{});
}

void Grid::clear()
{
    for (Cell& cell : m_cells)
    {
        cell = Cell{};
    }
}

int Grid::rows() const
{
    return m_rows;
}

int Grid::cols() const
{
    return m_cols;
}

bool Grid::in_bounds(int row, int col) const
{
    return row >= 0 && row < m_rows && col >= 0 && col < m_cols;
}

Cell& Grid::at(int row, int col)
{
    return m_cells.at(index_for(row, col));
}

const Cell& Grid::at(int row, int col) const
{
    return m_cells.at(index_for(row, col));
}

std::size_t Grid::index_for(int row, int col) const
{
    if (!in_bounds(row, col))
    {
        throw std::out_of_range("Grid coordinates out of bounds.");
    }
    return static_cast<std::size_t>(row * m_cols + col);
}

Arena::Arena()
    : m_grid(m_config.height, m_config.width),
      m_rng(std::random_device{}())
{
    m_robot_color_pool.assign(
        std::begin(kRobotPalette),
        std::end(kRobotPalette));
    std::shuffle(m_robot_color_pool.begin(), m_robot_color_pool.end(), m_rng);
}

Arena::~Arena()
{
    cleanup_robots();
}

bool Arena::load_config(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
    {
        std::cerr << "Failed to open config file: " << path << '\n';
        return false;
    }

    ArenaConfig parsed_config = m_config;
    std::string line;

    while (std::getline(input, line))
    {
        const std::string trimmed_line = trim(line);
        if (trimmed_line.empty())
        {
            continue;
        }

        const std::size_t colon = trimmed_line.find(':');
        if (colon == std::string::npos)
        {
            std::cerr << "Ignoring invalid config line: " << trimmed_line << '\n';
            continue;
        }

        const std::string key = trim(trimmed_line.substr(0, colon));
        const std::string value = trim(trimmed_line.substr(colon + 1));

        if (key == "Arena_Size")
        {
            std::istringstream size_stream(value);
            int height = 0;
            int width = 0;
            if (!(size_stream >> height >> width) || height <= 0 || width <= 0)
            {
                std::cerr << "Invalid Arena_Size value: " << value << '\n';
                return false;
            }
            parsed_config.height = height;
            parsed_config.width = width;
        }
        else if (key == "Max_Rounds")
        {
            std::istringstream rounds_stream(value);
            if (!(rounds_stream >> parsed_config.max_rounds) || parsed_config.max_rounds <= 0)
            {
                std::cerr << "Invalid Max_Rounds value: " << value << '\n';
                return false;
            }
        }
        else if (key == "Sleep_interval")
        {
            std::istringstream sleep_stream(value);
            if (!(sleep_stream >> parsed_config.sleep_interval) || parsed_config.sleep_interval < 0.0)
            {
                std::cerr << "Invalid Sleep_interval value: " << value << '\n';
                return false;
            }
        }
        else if (key == "Game_State_Live")
        {
            bool live = false;
            if (!parse_bool(value, live))
            {
                std::cerr << "Invalid Game_State_Live value: " << value << '\n';
                return false;
            }
            parsed_config.game_state_live = live;
        }
        else if (key == "Flamethrowers")
        {
            std::istringstream obstacle_stream(value);
            if (!(obstacle_stream >> parsed_config.flamethrowers) || parsed_config.flamethrowers < 0)
            {
                std::cerr << "Invalid Flamethrowers value: " << value << '\n';
                return false;
            }
        }
        else if (key == "Pits")
        {
            std::istringstream obstacle_stream(value);
            if (!(obstacle_stream >> parsed_config.pits) || parsed_config.pits < 0)
            {
                std::cerr << "Invalid Pits value: " << value << '\n';
                return false;
            }
        }
        else if (key == "Mounds")
        {
            std::istringstream obstacle_stream(value);
            if (!(obstacle_stream >> parsed_config.mounds) || parsed_config.mounds < 0)
            {
                std::cerr << "Invalid Mounds value: " << value << '\n';
                return false;
            }
        }
    }

    const int total_obstacles =
        parsed_config.flamethrowers + parsed_config.pits + parsed_config.mounds;
    const int arena_area = parsed_config.height * parsed_config.width;
    if (total_obstacles > arena_area)
    {
        std::cerr << "Too many obstacles for arena size.\n";
        return false;
    }

    m_config = parsed_config;
    m_grid.resize(m_config.height, m_config.width);
    return true;
}

void Arena::initialize()
{
    cleanup_robots();
    initialize_empty_board();
    place_obstacles();
    if (!load_robots())
    {
        throw std::runtime_error("Failed to load any robots.");
    }
    place_robots();
    m_round = 0;
}

void Arena::run()
{
    try
    {
        initialize();
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << '\n';
        return;
    }

    print_title_screen();

    for (m_round = 1; m_round <= m_config.max_rounds; ++m_round)
    {
        std::vector<std::string> turn_summaries;
        m_round_damage_events.clear();

        for (std::size_t index = 0; index < m_robots.size(); ++index)
        {
            int winner_index = -1;
            if (has_winner(winner_index))
            {
                print_frame_header();
                print_board();
                print_round_summary(turn_summaries, m_round_damage_events);
                std::cout << "\nWinner: "
                          << colored_robot_label(m_robots[static_cast<std::size_t>(winner_index)]) << '\n';
                print_post_match_summary(winner_index);
                return;
            }

            turn_summaries.push_back(resolve_robot_turn(static_cast<int>(index)));
        }

        print_frame_header();
        print_board();
        print_round_summary(turn_summaries, m_round_damage_events);

        int winner_index = -1;
        if (has_winner(winner_index))
        {
            std::cout << "\nWinner: "
                      << colored_robot_label(m_robots[static_cast<std::size_t>(winner_index)]) << '\n';
            print_post_match_summary(winner_index);
            return;
        }

        if (m_config.game_state_live && m_config.sleep_interval > 0.0)
        {
            std::this_thread::sleep_for(std::chrono::duration<double>(m_config.sleep_interval));
        }
    }

    int winner_index = -1;
    if (has_winner(winner_index))
    {
        std::cout << "\nWinner: "
                  << colored_robot_label(m_robots[static_cast<std::size_t>(winner_index)]) << '\n';
        print_post_match_summary(winner_index);
        return;
    }

    std::cout << "\nReached max rounds without a single winner.\n";
    int best_index = -1;
    for (std::size_t index = 0; index < m_robots.size(); ++index)
    {
        if (best_index == -1)
        {
            best_index = static_cast<int>(index);
            continue;
        }

        RobotBase* best = m_robots[static_cast<std::size_t>(best_index)].robot;
        RobotBase* candidate = m_robots[index].robot;
        if (candidate->get_health() > best->get_health() ||
            (candidate->get_health() == best->get_health() &&
             candidate->get_armor() > best->get_armor()) ||
            (candidate->get_health() == best->get_health() &&
             candidate->get_armor() == best->get_armor() &&
             candidate->get_grenades() > best->get_grenades()))
        {
            best_index = static_cast<int>(index);
        }
    }

    if (best_index >= 0)
    {
        std::cout << "Best survivor: "
                  << colored_robot_label(m_robots[static_cast<std::size_t>(best_index)]) << '\n';
        print_post_match_summary(best_index);
    }
}

void Arena::print_board() const
{
    std::cout << "    ";
    for (int col = 0; col < m_grid.cols(); ++col)
    {
        std::cout << std::setw(3) << col;
    }
    std::cout << '\n';

    for (int row = 0; row < m_grid.rows(); ++row)
    {
        std::cout << std::setw(3) << row << ' ';
        for (int col = 0; col < m_grid.cols(); ++col)
        {
            std::cout << ' ' << render_cell(row, col) << ' ';
        }
        std::cout << '\n';
    }
}

const ArenaConfig& Arena::config() const
{
    return m_config;
}

const Grid& Arena::grid() const
{
    return m_grid;
}

bool Arena::load_robots()
{
    const std::vector<std::filesystem::path> robot_sources = discover_robot_sources();
    if (robot_sources.empty())
    {
        std::cerr << "No Robot_*.cpp files found.\n";
        return false;
    }

    m_robots.clear();
    m_robot_color_pool.assign(
        std::begin(kRobotPalette),
        std::end(kRobotPalette));
    std::shuffle(m_robot_color_pool.begin(), m_robot_color_pool.end(), m_rng);

    for (std::size_t index = 0; index < robot_sources.size(); ++index)
    {
        const std::filesystem::path& source_path = robot_sources[index];
        const std::filesystem::path shared_lib_path =
            std::filesystem::current_path() / ("lib" + source_path.stem().string() + ".so");
        const char display_char = kRobotChars[index % (sizeof(kRobotChars) / sizeof(kRobotChars[0]))];

        if (!compile_robot_source(source_path, shared_lib_path))
        {
            continue;
        }

        if (!load_robot_library(source_path, shared_lib_path, display_char))
        {
            continue;
        }
    }

    if (m_robots.empty())
    {
        std::cerr << "Failed to load any robot libraries.\n";
        return false;
    }

    return true;
}

void Arena::cleanup_robots()
{
    for (RobotRecord& record : m_robots)
    {
        delete record.robot;
        record.robot = nullptr;

        if (record.handle != nullptr)
        {
            dlclose(record.handle);
            record.handle = nullptr;
        }
    }
    m_robots.clear();
}

std::vector<std::filesystem::path> Arena::discover_robot_sources() const
{
    std::vector<std::filesystem::path> sources;
    const std::filesystem::path root = std::filesystem::current_path();
    const std::filesystem::path robots_dir = root / "robots";

    auto collect = [&](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
        {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (filename.rfind("Robot_", 0) != 0 || !is_valid_robot_filename(filename))
            {
                continue;
            }

            sources.push_back(entry.path());
        }
    };

    collect(robots_dir);
    collect(root);
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    return sources;
}

bool Arena::compile_robot_source(const std::filesystem::path& source_path,
                                 const std::filesystem::path& shared_lib_path) const
{
    const std::string compile_cmd =
        "g++ -shared -fPIC -o \"" + shared_lib_path.string() + "\" \"" + source_path.string() +
        "\" RobotBase.o -I. -std=c++20";

    std::cout << "Compiling " << source_path.string() << " into " << shared_lib_path.string() << "...\n";
    if (std::system(compile_cmd.c_str()) != 0)
    {
        std::cerr << "Failed to compile " << source_path.string() << '\n';
        return false;
    }

    return true;
}

bool Arena::load_robot_library(const std::filesystem::path& source_path,
                               const std::filesystem::path& shared_lib_path,
                               char display_char)
{
    using RobotSummaryFn = const char* (*)();

    void* handle = dlopen(shared_lib_path.c_str(), RTLD_LAZY);
    if (handle == nullptr)
    {
        std::cerr << "Failed to load " << shared_lib_path.string() << ": " << dlerror() << '\n';
        return false;
    }

    RobotFactory create_robot = reinterpret_cast<RobotFactory>(dlsym(handle, "create_robot"));
    if (create_robot == nullptr)
    {
        std::cerr << "Missing create_robot in " << shared_lib_path.string() << ": " << dlerror() << '\n';
        dlclose(handle);
        return false;
    }

    RobotSummaryFn robot_summary = reinterpret_cast<RobotSummaryFn>(dlsym(handle, "robot_summary"));
    if (robot_summary == nullptr)
    {
        std::cerr << "Missing robot_summary in " << shared_lib_path.string() << ": " << dlerror() << '\n';
        dlclose(handle);
        return false;
    }

    const char* summary = robot_summary();
    if (summary == nullptr || std::strlen(summary) == 0 || std::strlen(summary) > kMaxRobotSummaryChars)
    {
        std::cerr << "Invalid robot_summary in " << shared_lib_path.string() << '\n';
        dlclose(handle);
        return false;
    }

    RobotBase* robot = create_robot();
    if (robot == nullptr)
    {
        std::cerr << "Failed to instantiate robot from " << shared_lib_path.string() << '\n';
        dlclose(handle);
        return false;
    }

    robot->set_boundaries(m_config.height, m_config.width);
    robot->m_character = display_char;
    if (robot->m_name == "Blank_Robot")
    {
        robot->m_name = source_path.stem().string();
    }

    RobotRecord record;
    record.robot = robot;
    record.handle = handle;
    record.source_name = source_path.stem().string();
    record.summary = summary;
    record.display_char = display_char;
    record.alive = robot->get_health() > 0;
    if (m_robots.size() < m_robot_color_pool.size())
    {
        record.color = m_robot_color_pool[m_robots.size()];
    }
    m_robots.push_back(record);

    std::cout << "Loaded " << colored_robot_label(record)
              << ": " << record.summary << '\n';
    return true;
}

void Arena::place_robots()
{
    const int arena_area = m_grid.rows() * m_grid.cols();
    const int static_obstacles = m_config.flamethrowers + m_config.pits + m_config.mounds;
    if (static_obstacles + static_cast<int>(m_robots.size()) > arena_area)
    {
        throw std::runtime_error("Not enough empty cells to place all robots.");
    }

    std::uniform_int_distribution<int> row_dist(0, m_grid.rows() - 1);
    std::uniform_int_distribution<int> col_dist(0, m_grid.cols() - 1);

    for (std::size_t index = 0; index < m_robots.size(); ++index)
    {
        while (true)
        {
            const int row = row_dist(m_rng);
            const int col = col_dist(m_rng);
            if (!is_cell_empty_for_placement(row, col))
            {
                continue;
            }

            m_grid.at(row, col).robot_index = static_cast<int>(index);
            m_robots[index].robot->move_to(row, col);
            break;
        }
    }
}

void Arena::initialize_empty_board()
{
    m_grid.clear();
}

void Arena::place_obstacles()
{
    place_random_terrain(TerrainType::Flame, m_config.flamethrowers);
    place_random_terrain(TerrainType::Pit, m_config.pits);
    place_random_terrain(TerrainType::Mound, m_config.mounds);
}

void Arena::place_random_terrain(TerrainType terrain, int count)
{
    if (count <= 0)
    {
        return;
    }

    std::uniform_int_distribution<int> row_dist(0, m_grid.rows() - 1);
    std::uniform_int_distribution<int> col_dist(0, m_grid.cols() - 1);

    int placed = 0;
    while (placed < count)
    {
        const int row = row_dist(m_rng);
        const int col = col_dist(m_rng);
        if (!is_cell_empty_for_placement(row, col))
        {
            continue;
        }

        m_grid.at(row, col).terrain = terrain;
        ++placed;
    }
}

bool Arena::is_cell_empty_for_placement(int row, int col) const
{
    const Cell& cell = m_grid.at(row, col);
    return cell.terrain == TerrainType::Empty && cell.robot_index == -1 && cell.dead_robot_index == -1;
}

bool Arena::has_winner(int& winner_index) const
{
    winner_index = -1;
    int living = 0;
    for (std::size_t index = 0; index < m_robots.size(); ++index)
    {
        if (!m_robots[index].alive)
        {
            continue;
        }

        ++living;
        winner_index = static_cast<int>(index);
    }

    return living == 1;
}

int Arena::living_robot_count() const
{
    int count = 0;
    for (const RobotRecord& record : m_robots)
    {
        if (record.alive)
        {
            ++count;
        }
    }
    return count;
}

void Arena::print_title_screen() const
{
    static const char* kTitleArt[] = {
        "██████╗  ██████╗ ██████╗  ██████╗ ████████╗██╗    ██╗ █████╗ ██████╗ ███████╗",
        "██╔══██╗██╔═══██╗██╔══██╗██╔═══██╗╚══██╔══╝██║    ██║██╔══██╗██╔══██╗╚══███╔╝",
        "██████╔╝██║   ██║██████╔╝██║   ██║   ██║   ██║ █╗ ██║███████║██████╔╝  ███╔╝ ",
        "██╔══██╗██║   ██║██╔══██╗██║   ██║   ██║   ██║███╗██║██╔══██║██╔══██╗ ███╔╝  ",
        "██║  ██║╚██████╔╝██████╔╝╚██████╔╝   ██║   ╚███╔███╔╝██║  ██║██║  ██║███████╗",
        "╚═╝  ╚═╝ ╚═════╝ ╚═════╝  ╚═════╝    ╚═╝    ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝"
    };

    std::cout << '\n';
    for (const char* line : kTitleArt)
    {
        std::cout << line << '\n';
    }

    std::cout << "\nContestants\n";
    for (const RobotRecord& record : m_robots)
    {
        std::cout << colored_robot_label(record)
                  << " | HP " << record.robot->get_health()
                  << " | AR " << record.robot->get_armor()
                  << " | MV " << record.robot->get_move_speed()
                  << " | WP " << weapon_name(record.robot->get_weapon())
                  << " | " << record.summary << '\n';
    }

    std::cout << "\nMatch begins in:\n";
    for (int count = 3; count >= 1; --count)
    {
        std::cout << count << "...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "FIGHT!\n\n";
}

std::string Arena::resolve_robot_turn(int robot_index)
{
    RobotRecord& record = m_robots[static_cast<std::size_t>(robot_index)];
    RobotBase* robot = record.robot;
    record.rounds_survived = m_round;

    if (!record.alive || robot->get_health() <= 0)
    {
        record.alive = false;
        return format_robot_status(robot_index, "is out.");
    }

    int radar_direction = 0;
    robot->get_radar_direction(radar_direction);
    std::vector<RadarObj> radar_results = scan_radar(robot_index, radar_direction);
    robot->process_radar_results(radar_results);

    int start_row = 0;
    int start_col = 0;
    robot->get_current_location(start_row, start_col);

    int shot_row = 0;
    int shot_col = 0;
    if (robot->get_shot_location(shot_row, shot_col))
    {
        ++record.shots_fired;
        const ShotResult shot_result = resolve_shot(robot_index, shot_row, shot_col);
        std::ostringstream detail;
        detail << "ACTION: Shot " << shot_result.weapon_type;
        if (!shot_result.target_label.empty())
        {
            detail << " at " << shot_result.target_label;
        }
        if (!shot_result.hit)
        {
            detail << " - MISS";
        }
        else
        {
            detail << " - ";
            for (std::size_t index = 0; index < shot_result.hit_details.size(); ++index)
            {
                if (index > 0)
                {
                    detail << ", ";
                }
                detail << shot_result.hit_details[index];
            }
        }
        return format_robot_status(robot_index, detail.str());
    }

    int move_direction = 0;
    int move_distance = 0;
    robot->get_move_direction(move_direction, move_distance);
    const MoveResult move_result = resolve_move(robot_index, move_direction, move_distance);

    std::ostringstream detail;
    if (move_result.stuck)
    {
        ++record.stuck_turns;
        detail << "ACTION: " << record.source_name << " is stuck!";
    }
    else if (move_result.blocked)
    {
        ++record.blocked_moves;
        detail << "ACTION: Move blocked by mound or obstacle from (" << move_result.start_row
               << ',' << move_result.start_col << ") to (" << move_result.end_row
               << ',' << move_result.end_col << ')';
    }
    else
    {
        record.distance_traveled +=
            std::max(std::abs(move_result.end_row - move_result.start_row),
                     std::abs(move_result.end_col - move_result.start_col));
        detail << "ACTION: Moved from (" << move_result.start_row << ','
               << move_result.start_col << ") to (" << move_result.end_row
               << ',' << move_result.end_col << ')';
    }
    return format_robot_status(robot_index, detail.str());
}

std::vector<RadarObj> Arena::scan_radar(int robot_index, int radar_direction) const
{
    std::vector<RadarObj> results;
    std::set<std::pair<int, int>> seen_cells;

    int origin_row = 0;
    int origin_col = 0;
    m_robots[static_cast<std::size_t>(robot_index)].robot->get_current_location(origin_row, origin_col);

    auto add_if_visible = [&](int row, int col) {
        if (!m_grid.in_bounds(row, col) || (row == origin_row && col == origin_col))
        {
            return;
        }
        if (!seen_cells.insert({row, col}).second)
        {
            return;
        }

        const char type = visible_cell_type(row, col);
        if (type != '.')
        {
            results.emplace_back(type, row, col);
        }
    };

    if (radar_direction == 0)
    {
        for (int delta_row = -1; delta_row <= 1; ++delta_row)
        {
            for (int delta_col = -1; delta_col <= 1; ++delta_col)
            {
                if (delta_row == 0 && delta_col == 0)
                {
                    continue;
                }
                add_if_visible(origin_row + delta_row, origin_col + delta_col);
            }
        }
        return results;
    }

    if (radar_direction < 1 || radar_direction > 8)
    {
        return results;
    }

    const int delta_row = directions[radar_direction].first;
    const int delta_col = directions[radar_direction].second;
    const std::pair<int, int> left = perpendicular_left(delta_row, delta_col);
    const std::pair<int, int> right = perpendicular_right(delta_row, delta_col);

    int row = origin_row + delta_row;
    int col = origin_col + delta_col;
    while (m_grid.in_bounds(row, col))
    {
        add_if_visible(row, col);
        add_if_visible(row + left.first, col + left.second);
        add_if_visible(row + right.first, col + right.second);
        row += delta_row;
        col += delta_col;
    }

    return results;
}

ShotResult Arena::resolve_shot(int robot_index, int shot_row, int shot_col)
{
    RobotBase* shooter = m_robots[static_cast<std::size_t>(robot_index)].robot;
    const WeaponType weapon = shooter->get_weapon();
    ShotResult result;
    result.weapon_type = weapon_name(weapon);
    result.fired = true;

    int origin_row = 0;
    int origin_col = 0;
    shooter->get_current_location(origin_row, origin_col);

    std::set<int> targets_hit;

    auto collect_targets = [&](const std::vector<std::pair<int, int>>& cells) {
        for (const auto& [row, col] : cells)
        {
            int target_index = -1;
            if (try_get_robot_at(row, col, target_index, false) && target_index != robot_index)
            {
                targets_hit.insert(target_index);
            }
        }
    };

    if (weapon == railgun)
    {
        collect_targets(build_line_cells(origin_row, origin_col, shot_row, shot_col, -1));
        for (int target_index : targets_hit)
        {
            result.hit_details.push_back(apply_damage_to_robot(target_index, 10, 20, "railgun", robot_index));
        }
    }
    else if (weapon == grenade)
    {
        if (shooter->get_grenades() <= 0)
        {
            return result;
        }

        const int center_row = std::clamp(shot_row, 0, m_grid.rows() - 1);
        const int center_col = std::clamp(shot_col, 0, m_grid.cols() - 1);
        for (int row = center_row - 1; row <= center_row + 1; ++row)
        {
            for (int col = center_col - 1; col <= center_col + 1; ++col)
            {
                int target_index = -1;
                if (try_get_robot_at(row, col, target_index, false) && target_index != robot_index)
                {
                    targets_hit.insert(target_index);
                }
            }
        }

        shooter->decrement_grenades();
        for (int target_index : targets_hit)
        {
            result.hit_details.push_back(apply_damage_to_robot(target_index, 10, 40, "grenade", robot_index));
        }
    }
    else if (weapon == flamethrower)
    {
        collect_targets(build_flamethrower_cells(origin_row, origin_col, shot_row, shot_col));
        for (int target_index : targets_hit)
        {
            result.hit_details.push_back(apply_damage_to_robot(target_index, 30, 50, "flamethrower", robot_index));
        }
    }
    else if (weapon == hammer)
    {
        collect_targets(build_line_cells(origin_row, origin_col, shot_row, shot_col, kHammerRange));
        for (int target_index : targets_hit)
        {
            result.hit_details.push_back(apply_damage_to_robot(target_index, 50, 60, "hammer", robot_index));
        }
    }

    result.hit = !targets_hit.empty();
    if (!targets_hit.empty())
    {
        const int primary_target = *targets_hit.begin();
        result.target_label = colored_robot_label(m_robots[static_cast<std::size_t>(primary_target)]);
    }
    m_robots[static_cast<std::size_t>(robot_index)].attacks_landed += static_cast<int>(targets_hit.size());
    return result;
}

MoveResult Arena::resolve_move(int robot_index, int direction, int distance)
{
    MoveResult result;

    if (direction < 1 || direction > 8 || distance <= 0)
    {
        return result;
    }

    RobotRecord& record = m_robots[static_cast<std::size_t>(robot_index)];
    RobotBase* robot = record.robot;
    result.attempted = true;
    const int max_distance = std::min(distance, robot->get_move_speed());
    if (max_distance <= 0)
    {
        int row = 0;
        int col = 0;
        robot->get_current_location(row, col);
        result.start_row = row;
        result.start_col = col;
        result.end_row = row;
        result.end_col = col;
        result.stuck = true;
        return result;
    }

    int current_row = 0;
    int current_col = 0;
    robot->get_current_location(current_row, current_col);
    const int start_row = current_row;
    const int start_col = current_col;
    result.start_row = start_row;
    result.start_col = start_col;
    result.end_row = start_row;
    result.end_col = start_col;

    const int delta_row = directions[direction].first;
    const int delta_col = directions[direction].second;

    int row = current_row;
    int col = current_col;

    for (int step = 0; step < max_distance; ++step)
    {
        const int next_row = row + delta_row;
        const int next_col = col + delta_col;
        if (!m_grid.in_bounds(next_row, next_col))
        {
            result.blocked = true;
            break;
        }

        const Cell& next_cell = m_grid.at(next_row, next_col);
        if (next_cell.robot_index >= 0 || next_cell.dead_robot_index >= 0 ||
            next_cell.terrain == TerrainType::Mound)
        {
            result.blocked = true;
            break;
        }

        row = next_row;
        col = next_col;
        robot->move_to(row, col);

        if (next_cell.terrain == TerrainType::Flame)
        {
            apply_damage_to_robot(robot_index, 30, 50, "arena flamethrower", -1);
            if (!record.alive)
            {
                m_grid.at(start_row, start_col).robot_index = -1;
                result.end_row = row;
                result.end_col = col;
                return result;
            }
        }

        if (next_cell.terrain == TerrainType::Pit)
        {
            m_grid.at(start_row, start_col).robot_index = -1;
            m_grid.at(row, col).robot_index = robot_index;
            robot->disable_movement();
            result.end_row = row;
            result.end_col = col;
            return result;
        }
    }

    if (row != start_row || col != start_col)
    {
        m_grid.at(start_row, start_col).robot_index = -1;
        m_grid.at(row, col).robot_index = robot_index;
    }
    else
    {
        robot->move_to(start_row, start_col);
    }

    result.end_row = row;
    result.end_col = col;
    return result;
}

std::string Arena::apply_damage_to_robot(int target_index, int min_damage, int max_damage,
                                         const std::string&, int source_robot_index)
{
    RobotRecord& target = m_robots[static_cast<std::size_t>(target_index)];
    if (!target.alive)
    {
        return "";
    }

    std::uniform_int_distribution<int> damage_dist(min_damage, max_damage);
    const int raw_damage = damage_dist(m_rng);
    const int armor = target.robot->get_armor();
    int reduced_damage = static_cast<int>(std::lround(raw_damage * (1.0 - (armor * 0.1))));
    if (reduced_damage < 0)
    {
        reduced_damage = 0;
    }
    const int blocked_damage = raw_damage - reduced_damage;

    const int remaining_health = target.robot->take_damage(reduced_damage);
    target.robot->reduce_armor(1);
    target.damage_taken += reduced_damage;
    target.damage_blocked += blocked_damage;

    if (source_robot_index >= 0)
    {
        RobotRecord& attacker = m_robots[static_cast<std::size_t>(source_robot_index)];
        attacker.damage_dealt += reduced_damage;
        if (remaining_health == 0)
        {
            ++attacker.kills;
        }
    }

    int row = 0;
    int col = 0;
    target.robot->get_current_location(row, col);

    std::ostringstream event;
    if (source_robot_index >= 0)
    {
        const RobotRecord& attacker = m_robots[static_cast<std::size_t>(source_robot_index)];
        event << colored_robot_label(attacker) << " did "
              << reduced_damage << " damage to " << colored_robot_label(target)
              << ", " << colored_robot_label(target) << " is now at "
              << remaining_health << " health";
    }
    else
    {
        event << "Arena hazard did " << reduced_damage << " damage to "
              << colored_robot_label(target) << ", " << colored_robot_label(target)
              << " is now at " << remaining_health << " health";
    }
    m_round_damage_events.push_back(event.str());

    std::ostringstream hit_detail;
    hit_detail << reduced_damage << " DMG to " << target.display_char;

    if (remaining_health > 0)
    {
        return hit_detail.str();
    }

    target.alive = false;
    Cell& cell = m_grid.at(row, col);
    cell.robot_index = -1;
    cell.dead_robot_index = target_index;
    if (cell.terrain == TerrainType::Flame)
    {
        cell.terrain = TerrainType::Empty;
    }

    return hit_detail.str();
}

std::vector<std::pair<int, int>> Arena::build_line_cells(int origin_row, int origin_col,
                                                         int target_row, int target_col,
                                                         int max_steps) const
{
    std::vector<std::pair<int, int>> cells;

    int delta_row = target_row - origin_row;
    int delta_col = target_col - origin_col;
    if (delta_row == 0 && delta_col == 0)
    {
        return cells;
    }

    const int steps = std::max(std::abs(delta_row), std::abs(delta_col));
    const double row_inc = static_cast<double>(delta_row) / static_cast<double>(steps);
    const double col_inc = static_cast<double>(delta_col) / static_cast<double>(steps);

    double row = static_cast<double>(origin_row);
    double col = static_cast<double>(origin_col);
    std::set<std::pair<int, int>> seen;

    int step_count = 0;
    while (true)
    {
        row += row_inc;
        col += col_inc;
        const int candidate_row = static_cast<int>(std::lround(row));
        const int candidate_col = static_cast<int>(std::lround(col));

        if (!m_grid.in_bounds(candidate_row, candidate_col))
        {
            break;
        }

        if (seen.insert({candidate_row, candidate_col}).second)
        {
            cells.push_back({candidate_row, candidate_col});
            ++step_count;
            if (max_steps > 0 && step_count >= max_steps)
            {
                break;
            }
        }
    }

    return cells;
}

std::vector<std::pair<int, int>> Arena::build_flamethrower_cells(int origin_row, int origin_col,
                                                                 int target_row, int target_col) const
{
    std::vector<std::pair<int, int>> cells;

    int delta_row = target_row - origin_row;
    int delta_col = target_col - origin_col;
    if (delta_row == 0 && delta_col == 0)
    {
        return cells;
    }

    const int step_row = (delta_row > 0) - (delta_row < 0);
    const int step_col = (delta_col > 0) - (delta_col < 0);
    const std::pair<int, int> left = perpendicular_left(step_row, step_col);
    const std::pair<int, int> right = perpendicular_right(step_row, step_col);

    for (int distance = 1; distance <= 4; ++distance)
    {
        const int center_row = origin_row + (step_row * distance);
        const int center_col = origin_col + (step_col * distance);
        const std::pair<int, int> locations[] = {
            {center_row, center_col},
            {center_row + left.first, center_col + left.second},
            {center_row + right.first, center_col + right.second}
        };

        for (const auto& location : locations)
        {
            if (m_grid.in_bounds(location.first, location.second))
            {
                cells.push_back(location);
            }
        }
    }

    return cells;
}

bool Arena::try_get_robot_at(int row, int col, int& robot_index, bool include_dead) const
{
    robot_index = -1;
    if (!m_grid.in_bounds(row, col))
    {
        return false;
    }

    const Cell& cell = m_grid.at(row, col);
    if (cell.robot_index >= 0)
    {
        robot_index = cell.robot_index;
        return true;
    }

    if (include_dead && cell.dead_robot_index >= 0)
    {
        robot_index = cell.dead_robot_index;
        return true;
    }

    return false;
}

char Arena::visible_cell_type(int row, int col) const
{
    if (!m_grid.in_bounds(row, col))
    {
        return '.';
    }

    const Cell& cell = m_grid.at(row, col);
    if (cell.robot_index >= 0)
    {
        return 'R';
    }
    if (cell.dead_robot_index >= 0)
    {
        return 'X';
    }
    return terrain_to_char(cell.terrain);
}

void Arena::print_frame_header() const
{
    std::cout << "\n=========== ROUND " << m_round
              << " | living robots: " << living_robot_count() << " ===========\n";
    if (m_round_damage_events.empty())
    {
        std::cout << "No damage dealt last round.\n\n";
        return;
    }

    for (const std::string& event : m_round_damage_events)
    {
        std::cout << event << '\n';
    }
    std::cout << '\n';
}

std::string Arena::render_cell(int row, int col) const
{
    const Cell& cell = m_grid.at(row, col);
    if (cell.robot_index >= 0)
    {
        const RobotRecord& record = m_robots[static_cast<std::size_t>(cell.robot_index)];
        return colorize(record.display_char, record.color);
    }
    if (cell.dead_robot_index >= 0)
    {
        return colorize("X", kDeadRobotColor);
    }

    const char terrain_char = terrain_to_char(cell.terrain);
    if (cell.terrain == TerrainType::Mound)
    {
        return colorize(terrain_char, kMoundColor);
    }
    if (cell.terrain == TerrainType::Pit)
    {
        return colorize(terrain_char, kPitColor);
    }
    if (cell.terrain == TerrainType::Flame)
    {
        return colorize(terrain_char, kFlameColor);
    }

    return std::string(1, terrain_char);
}

std::string Arena::format_robot_status(int robot_index, const std::string& detail) const
{
    const RobotRecord& record = m_robots[static_cast<std::size_t>(robot_index)];
    int row = 0;
    int col = 0;
    record.robot->get_current_location(row, col);

    std::ostringstream out;
    out << '[' << colored_robot_label(record) << ']'
        << "  HP " << record.robot->get_health()
        << "  AR " << record.robot->get_armor()
        << "  POS (" << row << ',' << col << ')'
        << '\n';
    out << "  " << detail;
    return out.str();
}

void Arena::print_round_summary(const std::vector<std::string>& turn_summaries,
                                const std::vector<std::string>&) const
{
    std::cout << '\n';
    for (const std::string& summary : turn_summaries)
    {
        std::cout << summary << '\n';
    }
}

void Arena::print_post_match_summary(int winner_index) const
{
    std::cout << "\n=========== POST-MATCH SUMMARY ===========\n";
    if (winner_index >= 0)
    {
        std::cout << "Winner: "
                  << colored_robot_label(m_robots[static_cast<std::size_t>(winner_index)]) << '\n';
    }

    const int offensive_index = best_robot_index(m_robots, [](const RobotRecord& record) {
        return record.attacks_landed;
    });
    const int defensive_index = best_robot_index(m_robots, [](const RobotRecord& record) {
        return record.damage_blocked;
    });
    const int damage_index = best_robot_index(m_robots, [](const RobotRecord& record) {
        return record.damage_dealt;
    });
    const int distance_index = best_robot_index(m_robots, [](const RobotRecord& record) {
        return record.distance_traveled;
    });
    const int accuracy_index = best_robot_index(m_robots, [](const RobotRecord& record) {
        if (record.shots_fired == 0)
        {
            return 0;
        }
        return (record.attacks_landed * 1000) / record.shots_fired;
    });
    const int kills_index = best_robot_index(m_robots, [](const RobotRecord& record) {
        return record.kills;
    });

    auto print_award = [&](const std::string& title, int robot_index, const std::string& value_text) {
        if (robot_index < 0)
        {
            return;
        }
        std::cout << title << ": "
                  << colored_robot_label(m_robots[static_cast<std::size_t>(robot_index)])
                  << " (" << value_text << ")\n";
    };

    print_award("Most Offensive", offensive_index,
                std::to_string(m_robots[static_cast<std::size_t>(offensive_index)].attacks_landed) + " landed hits");
    print_award("Most Defensive", defensive_index,
                std::to_string(m_robots[static_cast<std::size_t>(defensive_index)].damage_blocked) + " damage blocked");
    print_award("Most Damage", damage_index,
                std::to_string(m_robots[static_cast<std::size_t>(damage_index)].damage_dealt) + " damage dealt");
    print_award("Furthest Traveled", distance_index,
                std::to_string(m_robots[static_cast<std::size_t>(distance_index)].distance_traveled) + " cells");
    print_award("Most Accurate", accuracy_index,
                std::to_string((m_robots[static_cast<std::size_t>(accuracy_index)].shots_fired == 0)
                                   ? 0
                                   : ((m_robots[static_cast<std::size_t>(accuracy_index)].attacks_landed * 100) /
                                      m_robots[static_cast<std::size_t>(accuracy_index)].shots_fired)) +
                    "% accuracy");
    int lethal_index = kills_index;
    if (winner_index >= 0 &&
        m_robots[static_cast<std::size_t>(kills_index)].kills == 0)
    {
        lethal_index = winner_index;
    }
    print_award("Most Lethal", lethal_index,
                std::to_string(m_robots[static_cast<std::size_t>(lethal_index)].kills) + " kills");

    std::cout << "\nRobot Stats\n";
    for (const RobotRecord& record : m_robots)
    {
        std::cout << colored_robot_label(record)
                  << " | HP " << record.robot->get_health()
                  << " | AR " << record.robot->get_armor()
                  << " | DMG Out " << record.damage_dealt
                  << " | DMG In " << record.damage_taken
                  << " | Blocked " << record.damage_blocked
                  << " | Shots " << record.shots_fired
                  << " | Hits " << record.attacks_landed
                  << " | Kills " << record.kills
                  << " | Dist " << record.distance_traveled
                  << " | Blocked Moves " << record.blocked_moves
                  << " | Stuck " << record.stuck_turns
                  << " | Survived " << record.rounds_survived << " rounds\n";
    }
}

std::string Arena::trim(const std::string& value)
{
    std::size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start])) != 0)
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }

    return value.substr(start, end - start);
}

bool Arena::parse_bool(const std::string& value, bool& parsed_value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value)
    {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }

    if (lowered == "true")
    {
        parsed_value = true;
        return true;
    }
    if (lowered == "false")
    {
        parsed_value = false;
        return true;
    }

    return false;
}

bool Arena::is_valid_robot_filename(const std::string& filename)
{
    if (filename.size() <= 4 || filename.substr(filename.size() - 4) != ".cpp")
    {
        return false;
    }

    for (const unsigned char ch : filename)
    {
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.'))
        {
            return false;
        }
    }
    return true;
}
