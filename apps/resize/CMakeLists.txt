
halide_project(
    interpolate
    "apps" interpolate.cpp)

add_custom_command(
    OUTPUT out.png
    COMMAND "resize" "../images/rgba.png" "out.png"
            "-f" "2.0" "-t" "cubic" "-s" "3")

add_custom_target(
    "resize"
    DEPENDS interpolate)
