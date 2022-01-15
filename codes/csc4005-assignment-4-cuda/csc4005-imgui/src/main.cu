#include <graphic/graphic.hpp>
#include <imgui_impl_sdl.h>
#include <cstring>
#include <chrono>
//#include <hdist/hdist.hpp>

template<typename ...Args>
void UNUSED(Args &&... args [[maybe_unused]]) {}

ImColor temp_to_color(double temp) {
    auto value = static_cast<uint8_t>(temp / 100.0 * 255.0);
    return {value, 0, 255 - value};
}


__global__ void cuda_cal(int buffer_num_g[], float state_g[], bool stable_g[], double vec0_g[], double vec1_g[]){
    int room_size = state_g[0];
    float block_size = state_g[1];
    int source_x = state_g[2];
    int source_y = state_g[3];
    float source_temp = state_g[4];
    float border_temp = state_g[5];
    float tolerance = state_g[6];
    float sor_constant = state_g[7];
    int algo = state_g[8];
    int num_cuda = state_g[9];
    int buffer_num = buffer_num_g[0];

    // divide work first
    int workload_rows = room_size/num_cuda;
    int remainder = room_size%num_cuda;
    if (threadIdx.x < remainder){
        workload_rows ++;
    }
    int start_idx = 0;
    if (threadIdx.x>=remainder){
        start_idx = remainder*(workload_rows+1)+(threadIdx.x-remainder)*workload_rows;
    }else{
        start_idx = threadIdx.x*workload_rows;
    }

    // calculate
    if (buffer_num==0){//read from vec0, write into vec1
        for (int i=start_idx; i<start_idx+workload_rows; i++){
            for (int j=0; j<room_size; j++){
                if (i == 0 || j == 0 || i == room_size - 1 || j == room_size - 1) {
                    vec1_g[i * room_size + j] = border_temp;
                } else if (i == source_x && j == source_y) {
                    vec1_g[i * room_size + j] = source_temp;
                }else{
                    auto sum = (vec0_g[(i + 1) * room_size + j] + vec0_g[(i - 1) * room_size + j] + vec0_g[i * room_size + j + 1] + vec0_g[i * room_size + j - 1]);
                    switch (algo) {
                        case 0: // jacobi
                            vec1_g[i * room_size + j] = 0.25 * sum;
                            break;
                        case 1: // sor
                            vec1_g[i * room_size + j] = vec0_g[i * room_size + j] + (1.0 / sor_constant) * (sum - 4.0 * vec0_g[i * room_size + j]);
                            break;
                    }
                }
                switch (std::fabs(vec0_g[i * room_size + j] - vec1_g[i * room_size + j]) < tolerance){
                    case true:
                        stable_g[i * room_size + j] = true;
                        break;
                    case false:
                        stable_g[i * room_size + j] = false;
                        break;
                }
            }
        }
    }

    else if (buffer_num==1){//read from vec1, write into vec0
        for (int i=start_idx; i<start_idx+workload_rows; i++){
            for (int j=0; j<room_size; j++){
                if (i == 0 || j == 0 || i == room_size - 1 || j == room_size - 1) {
                    vec0_g[i * room_size + j] = border_temp;
                } else if (i == source_x && j == source_y) {
                    vec0_g[i * room_size + j] = source_temp;
                }else{
                    auto sum = (vec1_g[(i + 1) * room_size + j] + vec1_g[(i - 1) * room_size + j] + vec1_g[i * room_size + j + 1] + vec1_g[i * room_size + j - 1]);
                    switch (algo) {
                        case 0: // jacobi
                            vec0_g[i * room_size + j] = 0.25 * sum;
                            break;
                        case 1: // sor
                            vec0_g[i * room_size + j] = vec1_g[i * room_size + j] + (1.0 / sor_constant) * (sum - 4.0 * vec1_g[i * room_size + j]);
                            break;
                    }
                }
                switch (std::fabs(vec1_g[i * room_size + j] - vec0_g[i * room_size + j]) < tolerance){
                    case true:
                        stable_g[i * room_size + j] = true;
                        break;
                    case false:
                        stable_g[i * room_size + j] = false;
                        break;
                }
            }
        }
    }




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
    static float num_cuda = 4;
    static int room_size = 300;
    if (argc > 3) {
        std::cerr << "wrong arguments. please input only one argument as the number of threads" << std::endl;
        return 0;
    }else if (argc == 3){
        num_cuda = std::stoi(argv[1]);
        room_size = std::stoi(argv[2]);
    }

    bool first = true;
    bool finished = false;
//    static hdist::State current_state, last_state;
    static std::chrono::high_resolution_clock::time_point begin, end;
    static const char* algo_list[2] = { "jacobi", "sor" };
    graphic::GraphicContext context{"Assignment 4"};


    // init for cuda calculation
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

    float *state_c = (float*)malloc(sizeof(float)*10);
    float *state_c_old = (float*)malloc(sizeof(float)*10);
    double *vec0_c = (double*)malloc(sizeof(double)*room_size*room_size);
    double *vec1_c = (double*)malloc(sizeof(double)*room_size*room_size);
    int *buffer_num_c = (int*)malloc(sizeof(int)*1);
    bool *stable_c = (bool*)malloc(sizeof(bool)*room_size*room_size);

    buffer_num_c[0]=buffer_num;
    float *state_g;
    double *vec0_g, *vec1_g;
    bool *stable_g;
    int *buffer_num_g;
    cudaMalloc((void**)&state_g, sizeof(float)*10);
    cudaMalloc((void**)&vec0_g, sizeof(double)*room_size*room_size);
    cudaMalloc((void**)&vec1_g, sizeof(double)*room_size*room_size);
    cudaMalloc((void**)&stable_g, sizeof(bool)*room_size*room_size);
    cudaMalloc((void**)&buffer_num_g, sizeof(int)*1);

    for (int i=0; i<room_size*room_size; i++){
        vec0_c[i]=0;
        vec1_c[i]=0;
    }
    cudaMemcpy(vec0_g, vec0_c, sizeof(double)*room_size*room_size, cudaMemcpyHostToDevice);
    cudaMemcpy(vec1_g, vec1_c, sizeof(double)*room_size*room_size, cudaMemcpyHostToDevice);

    std::cout<<num_cuda<<std::endl;
    dim3 dimGrid(1);
    dim3 dimBlock(num_cuda);

    //loop below
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
        // no sor for now
        //ImGui::ListBox("Algorithm", reinterpret_cast<int *>(&algo), algo_list, 2);

//        if (algo == hdist::Algorithm::Sor) {
//            ImGui::DragFloat("Sor Constant", &sor_constant, 0.01, 0.0, 20.0, "%f");
//        }

        if (room_size != last_room_size) { // resize, restart if size changed
            last_room_size = room_size;
            std::cout<<"resize now"<<std::endl;

            first = true;
            finished = false;
            // init for cuda calculation
            state_c = (float*)malloc(sizeof(float)*10);
            vec0_c = (double*)malloc(sizeof(double)* room_size * room_size);
            vec1_c = (double*)malloc(sizeof(double)* room_size * room_size);
            buffer_num_c = (int*)malloc(sizeof(int)*1);
            stable_c = (bool*)malloc(sizeof(bool)* room_size * room_size);

            buffer_num_c[0]=buffer_num;
            cudaMalloc((void**)&state_g, sizeof(float)*10);
            cudaMalloc((void**)&vec0_g, sizeof(double) * room_size * room_size);
            cudaMalloc((void**)&vec1_g, sizeof(double) * room_size * room_size);
            cudaMalloc((void**)&stable_g, sizeof(bool) * room_size * room_size);
            cudaMalloc((void**)&buffer_num_g, sizeof(int)*1);

            for (int i=0; i<room_size*room_size; i++){
                vec0_c[i]=0;
                vec1_c[i]=0;
            }
            cudaMemcpy(vec0_g, vec0_c, sizeof(double) * room_size * room_size, cudaMemcpyHostToDevice);
            cudaMemcpy(vec1_g, vec1_c, sizeof(double) * room_size * room_size, cudaMemcpyHostToDevice);

        }


        state_c[0]=(int)room_size;
        state_c[1]=block_size;
        state_c[2]=(int)source_x;
        state_c[3]=(int)source_y;
        state_c[4]=source_temp;
        state_c[5]=border_temp;
        state_c[6]=tolerance;
        state_c[7]=sor_constant;
        state_c[8]=(int)algo;
        state_c[9]=(int)num_cuda;
        cudaMemcpy(state_g, state_c, sizeof(float)*10, cudaMemcpyHostToDevice);

        // check if old state and current state are same
        bool same_state = state_same(state_c, state_c_old, 10);
        if (!same_state){
            for (int i=0; i<10;i++){
                state_c_old[i] = state_c[i];
            }
            finished = false;
        }

        if (first) { // start timing
            first = false;
            finished = false;
            begin = std::chrono::high_resolution_clock::now();
        }

        if (!finished) {
            // need to return finished.
            cuda_cal<<<dimGrid, dimBlock>>>(buffer_num_g,state_g,stable_g,vec0_g,vec1_g);
            cudaMemcpy(vec0_c, vec0_g, sizeof(double)*room_size*room_size, cudaMemcpyDeviceToHost);
            cudaMemcpy(vec1_c, vec1_g, sizeof(double)*room_size*room_size, cudaMemcpyDeviceToHost);
            cudaMemcpy(stable_c, stable_g, sizeof(bool)*room_size*room_size, cudaMemcpyDeviceToHost);


            // debug use
//            std::cout<<"vec0 "<<std::endl;
//            for (int i=0;i<room_size;i++){
//                for (int j=0;j<room_size;j++){
//                    std::cout<<vec0_c[(int)room_size*i+j]<<" ";
//                }
//                std::cout<<std::endl;
//            }
//
//            std::cout<<"vec1 "<<std::endl;
//            for (int i=0;i<room_size;i++){
//                for (int j=0;j<room_size;j++){
//                    std::cout<<vec1_c[(int)room_size*i+j]<<" ";
//                }
//                std::cout<<std::endl;
//            }


            buffer_num = ((int)buffer_num+1)%2;
            buffer_num_c[0] = buffer_num;
            cudaMemcpy(buffer_num_g, buffer_num_c, sizeof(int)*1, cudaMemcpyHostToDevice);

            finished = stable_or_not(stable_c, room_size*room_size);

            if (finished) {
                end = std::chrono::high_resolution_clock::now();
            }

        }else{
            ImGui::Text("stabilized in %ld ns", std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
            size_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            std::cout<<"stabilized in "<<duration<<" ns"<<std::endl;

        }

        const ImVec2 p = ImGui::GetCursorScreenPos();
        float x = p.x + block_size, y = p.y + block_size;
        for (size_t i = 0; i < room_size; ++i) {
            for (size_t j = 0; j < room_size; ++j) {
                auto temp = 0;
                if (buffer_num == 0){
                    temp = vec0_c[i*room_size+j];
                }
                else if (buffer_num == 1){
                    temp = vec1_c[i*room_size+j];
                }

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
