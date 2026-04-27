#include "RobotBase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class Robot_Anvil : public RobotBase
{
private:
    int m_target_row = -1;
    int m_target_col = -1;
    bool m_has_target = false;
    int m_scan_direction = 3;
    bool m_patrol_flip = false;

    static int manhattan_distance(int row_a, int col_a, int row_b, int col_b)
    {
        return std::abs(row_a - row_b) + std::abs(col_a - col_b);
    }

    static int choose_direction(int row_delta, int col_delta)
    {
        for (int direction = 1; direction <= 8; ++direction)
        {
            if (directions[direction].first == row_delta &&
                directions[direction].second == col_delta)
            {
                return direction;
            }
        }
        return 0;
    }

public:
    Robot_Anvil() : RobotBase(2, 5, grenade)
    {
        m_name = "Anvil";
    }

    void get_radar_direction(int& radar_direction) override
    {
        radar_direction = m_scan_direction;
        m_scan_direction = (m_scan_direction % 8) + 1;
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        int best_score = std::numeric_limits<int>::min();
        m_has_target = false;

        for (const RadarObj& obj : radar_results)
        {
            if (obj.m_type != 'R')
            {
                continue;
            }

            int score = 100 - manhattan_distance(current_row, current_col, obj.m_row, obj.m_col);
            if (obj.m_col <= 1)
            {
                score += 40;
            }

            if (score > best_score)
            {
                best_score = score;
                m_target_row = obj.m_row;
                m_target_col = obj.m_col;
                m_has_target = true;
            }
        }
    }

    bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (!m_has_target || get_grenades() <= 0)
        {
            return false;
        }

        // Lead wall-hugging / linear movers by one row toward likely next position.
        shot_row = m_target_row;
        shot_col = m_target_col;
        if (m_target_col <= 1)
        {
            int current_row = 0;
            int current_col = 0;
            get_current_location(current_row, current_col);
            shot_row += (m_target_row > current_row) ? 1 : -1;
            shot_row = std::clamp(shot_row, 0, m_board_row_max - 1);
        }
        return true;
    }

    void get_move_direction(int& move_direction, int& move_distance) override
    {
        const int move_speed = get_move_speed();
        if (move_speed <= 0)
        {
            move_direction = 0;
            move_distance = 0;
            return;
        }

        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        const int board_center_row = (m_board_row_max - 1) / 2;
        const int board_center_col = (m_board_col_max - 1) / 2;

        if (!m_has_target)
        {
            const int row_delta = (board_center_row > current_row) - (board_center_row < current_row);
            const int col_delta = (board_center_col > current_col) - (board_center_col < current_col);
            move_direction = choose_direction(row_delta, col_delta);
            if (move_direction == 0)
            {
                move_direction = m_patrol_flip ? 2 : 6;
                move_distance = 1;
                m_patrol_flip = !m_patrol_flip;
                return;
            }
            move_distance = 1;
            return;
        }

        // Stay near center line and adjust slowly for bombardment angles.
        const int row_delta = (m_target_row > current_row) - (m_target_row < current_row);
        int col_delta = 0;
        if (current_col < board_center_col - 1)
        {
            col_delta = 1;
        }
        else if (current_col > board_center_col + 1)
        {
            col_delta = -1;
        }

        move_direction = choose_direction(row_delta, col_delta);
        if (move_direction == 0)
        {
            move_direction = m_patrol_flip ? 3 : 7;
            move_distance = 1;
            m_patrol_flip = !m_patrol_flip;
            return;
        }
        move_distance = 1;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_Anvil();
}

extern "C" const char* robot_summary()
{
    return "Armored grenadier that punishes railgun lanes.";
}
