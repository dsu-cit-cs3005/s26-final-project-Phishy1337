#include "RobotBase.h"

#include <cstdlib>
#include <ctime>
#include <vector>

class Robot_Schizobot : public RobotBase
{
private:
    bool m_should_shoot = false;
    int m_random_row = 0;
    int m_random_col = 0;
    bool m_has_seen_target = false;
    int m_seen_target_row = 0;
    int m_seen_target_col = 0;

public:
    Robot_Schizobot() : RobotBase(5, 2, railgun)
    {
        m_name = "Schizobot";
        std::srand(static_cast<unsigned int>(std::time(nullptr)) ^ 0x5EEDB0);
    }

    void get_radar_direction(int& radar_direction) override
    {
        radar_direction = (std::rand() % 9);
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        m_has_seen_target = false;
        for (const RadarObj& obj : radar_results)
        {
            if (obj.m_type == 'R')
            {
                m_has_seen_target = true;
                m_seen_target_row = obj.m_row;
                m_seen_target_col = obj.m_col;
                break;
            }
        }

        if (m_has_seen_target)
        {
            m_should_shoot = true;
            m_random_row = m_seen_target_row;
            m_random_col = m_seen_target_col;
            return;
        }

        m_should_shoot = (std::rand() % 100) < 40;
        m_random_row = std::rand() % m_board_row_max;
        m_random_col = std::rand() % m_board_col_max;
    }

    bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (!m_should_shoot)
        {
            return false;
        }

        shot_row = m_random_row;
        shot_col = m_random_col;
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

        move_direction = (std::rand() % 8) + 1;
        move_distance = (std::rand() % move_speed) + 1;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_Schizobot();
}

extern "C" const char* robot_summary()
{
    return "Erratic railgun bot with random shots and moves.";
}
