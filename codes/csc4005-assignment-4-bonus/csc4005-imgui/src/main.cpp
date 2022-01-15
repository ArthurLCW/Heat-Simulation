#include <graphic/graphic.hpp>
#include <imgui_impl_sdl.h>
#include <cstring>
#include <chrono>
//#include <hdist/hdist.hpp>
#include <mpi.h>
#include <omp.h>
#include <cmath>

template<typename ...Args>
void UNUSED(Args &&... args [[maybe_unused]]) {}

ImColor temp_to_color(double temp) {
    auto value = static_cast<uint8_t>(temp / 100.0 * 255.0);
    return {value, 0, 255 - value};
}

bool stable_or_not(bool stable_arr[], int len){
    for (int i=0; i<len; i++){
        if (!stable_arr[i]){
            return false;
        }
    }
    return true;
}

bool state_same( float a1[],float a2[], int len){
    for (int i=0; i<len; i++){
        if (a1[i]!=a2[i]){
            return false;
        }
    }
    return true;
}


int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank_num;
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &rank_num);

    std::cout<<rank_num<<std::endl;

    int room_size = 300; //300
    int thread_num = 2;
    if (argc > 3) {
        std::cerr << "wrong arguments. please input only one argument as the number of threads" << std::endl;
        return 0;
    }else if (argc == 3){ // first thread_num, then assigned_room_size.
        thread_num = std::stoi(argv[1]);
        room_size = std::stoi(argv[2]);
    }
    std::cout<<thread_num<<std::endl;
    omp_set_num_threads(thread_num);


    //loop below
    if (rank==0){
        bool print_one_time = false;
        bool first = true;
        bool finished = false;

        static std::chrono::high_resolution_clock::time_point begin, end;
        graphic::GraphicContext context{"Assignment 4"};

        // init for process calculation
        // replace original template

        float block_size = 2;
        int source_x = room_size / 2; // originally int
        int source_y = room_size / 2; // originally int
        float source_temp = 100;
        float border_temp = 36;
        float tolerance = 0.02;
        float sor_constant = 4.0;
        int algo = 0; // originally int
        int buffer_num = 0; // originally int

        int last_room_size = room_size;

        float *state = new float[12];
        float *state_old = new float[12];
        double *data = new double[room_size*room_size];


        for (int i=0; i < room_size * room_size; i++){
            data[i]=0;
        }

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
            ImGui::DragInt("Room Size", &room_size, 10, 200, 1600, "%d");
            ImGui::DragFloat("Block Size", &block_size, 0.01, 0.1, 10, "%f");
            ImGui::DragFloat("Source Temp", &source_temp, 0.1, 0, 100, "%f");
            ImGui::DragFloat("Border Temp", &border_temp, 0.1, 0, 100, "%f");
            ImGui::DragInt("Source X", &source_x, 1, 1, room_size - 2, "%d");
            ImGui::DragInt("Source Y", &source_y, 1, 1, room_size - 2, "%d");
            ImGui::DragFloat("Tolerance", &tolerance, 0.01, 0.01, 1, "%f");
            // no sor for now (not required in the manual).
            // ImGui::ListBox("Algorithm", reinterpret_cast<int *>(&algo), algo_list, 2);

            if (room_size != last_room_size) { // resize, restart if size changed
                last_room_size = room_size;
                std::cout<<"resize now"<<std::endl;

                first = true;
                finished = false;

                data = new double[room_size*room_size];

                for (int i=0; i<room_size*room_size; i++){
                    data[i]=0;
                }
            }


            state[0]=(int)room_size;
            state[1]=block_size;
            state[2]=(int)source_x;
            state[3]=(int)source_y;
            state[4]=source_temp;
            state[5]=border_temp;
            state[6]=tolerance;
            state[7]=sor_constant;
            state[8]=(int)algo;
            state[9]=(int)rank_num;
            state[10]=0; // how many rows to deal with (not including the reference row).
            state[11]=0;

            // check if old state and current state are same
            bool same_state = state_same(state, state_old, 10);
            if (!same_state){
                for (int i=0; i<12;i++){
                    state_old[i] = state[i];
                }
                finished = false;
            }

            if (first) { // start timing
                first = false;
                finished = false;
                begin = std::chrono::high_resolution_clock::now();
            }

            int workload_rows = room_size/rank_num;
            int remainder = room_size%rank_num;
            int *finished_indicator = new int[1];
            if (finished) finished_indicator[0] = 1;
            else if (!finished) finished_indicator[0] = 0;

            // tell other process if they should recv data from rank 0
            for (int i=1; i<rank_num; i++){
                MPI_Ssend(finished_indicator, 1, MPI_INT, i, 666, MPI_COMM_WORLD); // tag 666
            }
            int *ack_info = new int[1];

            if (!finished) {
                // send job to the other process
                // divide work first
                int current_row_idx = 0;
                if (remainder>0){
                    current_row_idx += workload_rows + 1;
                }else{
                    current_row_idx += workload_rows;
                }

                // sending state data and data
                for (int i = 1; i<rank_num; i++){
                    // sending state sending data
                    state[11] = current_row_idx;
                    int local_workload = workload_rows;
                    if (remainder > 0 && i < remainder) local_workload++;
                    state[10] = local_workload;
                    MPI_Send(state, 12, MPI_FLOAT, i, 0, MPI_COMM_WORLD); // tag 0

                    bool last_row = false;
                    if (i==rank_num - 1) last_row = true;

                    if (!last_row){ // not the last row
                        MPI_Send(data + room_size * (current_row_idx-1), room_size * (local_workload+2), MPI_DOUBLE, i, 1, MPI_COMM_WORLD); // tag 1
                    }else{ // the last row
                        MPI_Send(data + room_size * (current_row_idx-1), room_size * (local_workload+1), MPI_DOUBLE, i, 1, MPI_COMM_WORLD); // tag 1
                    }
                    current_row_idx += local_workload;
                }

                // calculate the rank 0 work
                int local_workload = workload_rows;
                if (remainder > 0) local_workload ++;

                double *reference_data = new double[(local_workload+1) * room_size]; // with reference
                for (int i=0; i<(local_workload+1) * room_size; i++){
                    reference_data[i] = data[i];
                }
                bool *stable = new bool[local_workload * room_size];

                int j=0;
                #pragma omp parallel for private(j)
                for (int i=0; i<local_workload; i++){
                    for (j=0; j<room_size; j++){
                        if (i == 0 || j == 0 || i == room_size - 1 || j == room_size - 1) {
                            data[i * room_size + j] = border_temp;
                        } else if (i == source_x && j == source_y) {
                            data[i * room_size + j] = source_temp;
                        }else{
                            auto sum = (reference_data[(i + 1) * room_size + j] + reference_data[(i - 1) * room_size + j] + reference_data[i * room_size + j + 1] + reference_data[i * room_size + j - 1]);
                            data[i * room_size + j] = 0.25 * sum;
                        }
                        switch (std::fabs(data[i * room_size + j] - reference_data[i * room_size + j]) < tolerance){
                            case true:
                                stable[i * room_size + j] = true;
                                break;
                            case false:
                                stable[i * room_size + j] = false;
                                break;
                        }
                    }
                }

                // receive the data_back;
                bool *stable_info = new bool[rank_num];
                stable_info[0] = stable_or_not(stable, local_workload * room_size);
                for (int i=1; i<rank_num; i++){
                    int *local_stable = new int[1];
                    MPI_Recv(local_stable, 1, MPI_INT, i, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // tag2

                    if (local_stable[0] == 0) stable_info[i] = false;
                    else stable_info[i] = true;
                }

                int starting_row_idx=local_workload;
                for (int i=1; i<rank_num; i++){
                    local_workload = workload_rows;
                    if (remainder > 0 && i<remainder)  local_workload++;

                    double *local_data = new double[local_workload*room_size];
                    MPI_Recv(local_data, local_workload*room_size, MPI_DOUBLE, i, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    for (int j=0; j<local_workload; j++){
                        for (int k=0; k<room_size; k++){
                            data[(j+starting_row_idx)*room_size+k] = local_data[j*room_size+k];
                        }
                    }
                    starting_row_idx += local_workload;
                }


                // sending ack signal to all
                for (int i=1; i<rank_num; i++){
                    ack_info[0]=1;
                    MPI_Send(ack_info, 1, MPI_INT, i, 66, MPI_COMM_WORLD);
                }


                // debug use
//                std::cout<<"data "<<std::endl;
//                for (int i=0;i<room_size;i++){
//                    for (int j=0;j<room_size;j++){
//                        std::cout<<data[(int)room_size*i+j]<<" ";
//                    }
//                    std::cout<<std::endl;
//                }
//                std::cout<<"stable "<<std::endl;
//                for (int i=0;i<rank_num;i++){
//                    std::cout<<stable_info[i]<<" ";
//                    std::cout<<std::endl;
//                }
//
//                std::cout<<std::endl;

    //            std::cout<<"vec1 "<<std::endl;
    //            for (int i=0;i<room_size;i++){
    //                for (int j=0;j<room_size;j++){
    //                    std::cout<<vec1_c[(int)room_size*i+j]<<" ";
    //                }
    //                std::cout<<std::endl;
    //            }

                    finished = stable_or_not(stable_info, rank_num);

                    if (finished) {
                        end = std::chrono::high_resolution_clock::now();
                    }

            }else{
                ImGui::Text("stabilized in %ld ns", std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
                if (!print_one_time){
                    size_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
                    std::cout<<"stabilized in "<<duration<<" ns."<<std::endl;
                    print_one_time = true;
                }
            }



            const ImVec2 p = ImGui::GetCursorScreenPos();
            float x = p.x + block_size, y = p.y + block_size;
            for (size_t i = 0; i < room_size; ++i) {
                for (size_t j = 0; j < room_size; ++j) {
                    auto temp = 0;
                    temp = data[i*room_size+j];


                    auto color = temp_to_color(temp);
                    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + block_size, y + block_size), color);
                    y += block_size;
                }
                x += block_size;
                y = p.y + block_size;
            }
            ImGui::End();
        });
    }

    else if (rank!=0){
        while (true){
            // init array, etc
            float *state = new float[12];

            int *finished_indicator = new int[1]; // 0: not finished; 1 finished
            MPI_Recv(finished_indicator, 1, MPI_INT, 0, 666, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (finished_indicator[0] == 0){ // not finished
                // recv state first
                MPI_Recv(state, 12, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                int room_size = state[0];
                float block_size = state[1];
                int source_x = state[2];
                int source_y = state[3];
                float source_temp = state[4];
                float border_temp = state[5];
                float tolerance = state[6];
                float sor_constant = state[7];
                int algo = state[8];
                int num_cuda = state[9];
                int local_workload = state[10];
                int current_row_idx = state[11];

                int num_ref = 2;
                if (rank==rank_num-1){ // last one
                    num_ref = 1;
                }
                double* data = new double[(local_workload+num_ref) * room_size];
                MPI_Recv(data, (local_workload+num_ref) * room_size, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                double* data_return = new double[local_workload * room_size];
                bool *stable = new bool[(local_workload) * room_size];
                int *stable_return = new int[1];

                int j=0;
                #pragma omp parallel for private(j)
                for (int i=0; i<local_workload; i++){ // 0th is a ref in data
                    //debug use
                    //std::cout<<"info: "+std::to_string(omp_get_num_threads())+" "+std::to_string(omp_get_thread_num())<<std::endl;


                    for ( j=0; j<room_size; j++){
                        if (j == 0 || i + current_row_idx == room_size-1 || j == room_size - 1) {
                            data_return[i * room_size + j] = border_temp;

                        } else if (i+current_row_idx == source_x && j == source_y) {
                            data_return[i * room_size + j] = source_temp;
                        }else{
                            auto sum = (data[(i + 1 +1) * room_size + j] + data[(i - 1 +1) * room_size + j] + data[(i +1) * room_size + j + 1] + data[(i +1) * room_size + j - 1]);
                            data_return[i * room_size + j] = 0.25 * sum;
                        }
                        switch (std::fabs(data[(i + 1) * room_size + j] - data_return[i * room_size + j]) < tolerance){
                            case true:
                                stable[i * room_size + j] = true;
                                break;
                            case false:
                                stable[i * room_size + j] = false;
                                break;
                        }
                    }
                }

                // check stable?
                if (stable_or_not(stable, local_workload * room_size)) stable_return[0] = 1;
                else stable_return[0] = 0;
                MPI_Send(stable_return, 1, MPI_INT, 0, 2, MPI_COMM_WORLD); // sending stable tag2
                MPI_Send(data_return, local_workload * room_size, MPI_DOUBLE, 0, 3, MPI_COMM_WORLD); // data sending tag3
                int *ack_info = new int[1];
                MPI_Recv(ack_info, 1, MPI_INT, 0, 66, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // ack 66
            }
        }
    }
}
