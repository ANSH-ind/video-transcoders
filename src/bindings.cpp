#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace py = pybind11;

// Tell the compiler this function exists in your other file (hls_engine.cpp)
void hls_encoder(std::string input, std::string output, std::vector<std::string> resolution, std::vector<int> bitrate, std::string keyinfo);

PYBIND11_MODULE(video_transcoders, m) {
    m.doc() = "HLS Video Transcoding C++ Engine Binding that gave the real power of c++ in python"; 

    m.def("hls_encoder", &hls_encoder, "Transcode video to HLS with dynamic ABR",
          py::arg("input"),
          py::arg("output"),
          py::arg("resolution") = std::vector<std::string>{"1080p", "720p", "480p", "360p"},
          py::arg("bitrate") = std::vector<int>{5000000, 2500000, 1000000, 400000},
          py::arg("key") = "enc.keyinfo"  // Now you can pass the key file from Python!
    );
}
