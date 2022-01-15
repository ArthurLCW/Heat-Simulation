#include <graphic/graphic.hpp>
#include <imgui_impl_sdl.h>
#include <cstring>
#include <chrono>
#include <hdist/hdist.hpp>
#include <omp.h>

template<typename ...Args>
void UNUSED(Args &&... args [[maybe_unused]]) {}

ImColor temp_to_color(double temp) {
    auto value = static_cast<uint8_t>(temp / 100.0 * 255.0);
    return {value, 0, 255 - value};
}

int main(int argc, char **argv) {
    // UNUSED(argc, argv);
    int assigned_room_size = 300;
    int thread_num=4;

    if (argc > 3) {
        std::cerr << "wrong arguments. please input only one argument as the number of threads" << std::endl;
        return 0;
    }else if (argc == 3){ // first thread_num, then assigned_room_size.
        thread_num = std::stoi(argv[1]);
        assigned_room_size = std::stoi(argv[2]);
    }

    omp_set_num_threads(thread_num);


    bool first = true;
    bool finished = false;
    static hdist::State current_state, last_state;

    bool first_print = false;

    current_state.room_size=assigned_room_size;
    current_state.source_x = assigned_room_size/2;
    current_state.source_y = assigned_room_size/2;
    last_state=current_state;

    static std::chrono::high_resolution_clock::time_point begin, end;
    static const char* algo_list[2] = { "jacobi", "sor" };
    graphic::GraphicContext context{"Assignment 4"};
    auto grid = hdist::Grid{
            static_cast<size_t>(current_state.room_size),
            current_state.border_temp,
            current_state.source_temp,
            static_cast<size_t>(current_state.source_x),
            static_cast<size_t>(current_state.source_y)};

    context.run([&](graphic::GraphicContext *context [[maybe_unused]], SDL_Window *) {
        auto io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Assignment 4", nullptr,
                     ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoCollapse
                     | ImGuiWindowFlags_NoTitleBar
                     | ImGuiWindowFlags_NoResize);
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);
        ImGui::DragInt("Room Size", &current_state.room_size, 10, 200, 1600, "%d");
        ImGui::DragFloat("Block Size", &current_state.block_size, 0.01, 0.1, 10, "%f");
        ImGui::DragFloat("Source Temp", &current_state.source_temp, 0.1, 0, 100, "%f");
        ImGui::DragFloat("Border Temp", &current_state.border_temp, 0.1, 0, 100, "%f");
        ImGui::DragInt("Source X", &current_state.source_x, 1, 1, current_state.room_size - 2, "%d");
        ImGui::DragInt("Source Y", &current_state.source_y, 1, 1, current_state.room_size - 2, "%d");
        ImGui::DragFloat("Tolerance", &current_state.tolerance, 0.01, 0.01, 1, "%f");
        ImGui::ListBox("Algorithm", reinterpret_cast<int *>(&current_state.algo), algo_list, 2);

        if (current_state.algo == hdist::Algorithm::Sor) {
            ImGui::DragFloat("Sor Constant", &current_state.sor_constant, 0.01, 0.0, 20.0, "%f");
        }

        if (current_state.room_size != last_state.room_size) { // resize, restart
            grid = hdist::Grid{
                    static_cast<size_t>(current_state.room_size),
                    current_state.border_temp,
                    current_state.source_temp,
                    static_cast<size_t>(current_state.source_x),
                    static_cast<size_t>(current_state.source_y)};
            first = true;
        }

        if (current_state != last_state) { // update states to see if unchanged
            last_state = current_state;
            finished = false;
        }

        if (first) { // start timing 
            first = false;
            finished = false;
            begin = std::chrono::high_resolution_clock::now();
        }

        if (!finished) {
            // finished = hdist::calculate(current_state, grid); // seq version
            bool stabilized = true;

            size_t j = 0;
            switch (current_state.algo) {
                case hdist::Algorithm::Jacobi:
                    #pragma omp parallel for private(j)
                    for (size_t i = 0; i < current_state.room_size; ++i) {
                        //debug use
                        //std::cout<<std::to_string(omp_get_num_threads())+" "+std::to_string(omp_get_thread_num())<<std::endl;
                        for (j = 0; j < current_state.room_size; ++j) {
                            auto result = update_single(i, j, grid, current_state);
                            stabilized &= result.stable; //& one false, all false
                            grid[{hdist::alt, i, j}] = result.temp;
                        }
                    }
                    grid.switch_buffer();
                    break;
                case hdist::Algorithm::Sor:
                    for (auto k : {0, 1}) {
                        #pragma omp parallel for private(j)
                        for (size_t i = 0; i < current_state.room_size; i++) {
                            for ( j = 0; j < current_state.room_size; j++) {
                                if (k == ((i + j) & 1)) {
                                    auto result = update_single(i, j, grid, current_state);
                                    stabilized &= result.stable;
                                    grid[{hdist::alt, i, j}] = result.temp;
                                } else {
                                    grid[{hdist::alt, i, j}] = grid[{i, j}];
                                }
                            }
                        }
                        grid.switch_buffer();
                    }
            }
            finished = stabilized;

            if (finished) end = std::chrono::high_resolution_clock::now();
        } else {
            ImGui::Text("stabilized in %ld ns", std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
            if (!first_print){
                size_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
                std::cout<<"stabilized in "<<duration<<" ns."<<std::endl;
                first_print = true;
            }
        }


        

        const ImVec2 p = ImGui::GetCursorScreenPos();
        float x = p.x + current_state.block_size, y = p.y + current_state.block_size;
        for (size_t i = 0; i < current_state.room_size; ++i) {
            for (size_t j = 0; j < current_state.room_size; ++j) {
                auto temp = grid[{i, j}];
                auto color = temp_to_color(temp);
                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + current_state.block_size, y + current_state.block_size), color);
                y += current_state.block_size;
            }
            x += current_state.block_size;
            y = p.y + current_state.block_size;
        }
        ImGui::End();
    });
}
