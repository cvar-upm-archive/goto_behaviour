from os.path import join

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    config = join(
        get_package_share_directory('goto_behaviour'),
        'config',
        'goto_behaviour.yaml'
    )

    return LaunchDescription([
        DeclareLaunchArgument('drone_id', default_value='drone0'),
        Node(
            package='goto_behaviour',
            executable='goto_behaviour_node',
            name='goto_behaviour_node',
            namespace=LaunchConfiguration('drone_id'),
            parameters=[config],
            output='screen',
            emulate_tty=True
        )
    ])