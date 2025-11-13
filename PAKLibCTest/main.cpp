#include <chrono>
#include <iostream>
#include <fstream>

#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif

#include "PAK.h"

inline std::vector<std::string> get_files_with_extension(const std::string & directory, const std::string & extension) {
    std::vector<std::string> files;

#if defined(_WIN32)
    std::string search_path = directory;
    if (!search_path.empty() && search_path.back() != '\\' && search_path.back() != '/')
        search_path += '\\';
    search_path += "*." + extension;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.emplace_back(fd.cFileName);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

#else
    DIR* dir = opendir(directory.c_str());
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > extension.size() &&
            name.substr(name.size() - extension.size()) == extension)
            files.emplace_back(name);
    }
    closedir(dir);
#endif

    return files;
}

//#if defined(_WIN32)
//constexpr char PATH_SEP = '\\';
//constexpr char ALT_SEP = '/';
//#else
//constexpr char PATH_SEP = '/';
//constexpr char ALT_SEP = '\\';
//#endif

inline std::string get_filename(const std::string& path) {
    if (path.empty())
        return {};

    size_t pos1 = path.find_last_of(PATH_SEP);
    size_t pos2 = path.find_last_of(ALT_SEP);
    size_t pos = std::string::npos;

    if (pos1 != std::string::npos && pos2 != std::string::npos)
        pos = (pos1 > pos2) ? pos1 : pos2;
    else if (pos1 != std::string::npos)
        pos = pos1;
    else if (pos2 != std::string::npos)
        pos = pos2;

    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

inline std::string get_filename_no_ext(const std::string& path) {
    if (path.empty())
        return {};

    size_t pos1 = path.find_last_of(PATH_SEP);
    size_t pos2 = path.find_last_of(ALT_SEP);
    size_t pos = std::string::npos;

    if (pos1 != std::string::npos && pos2 != std::string::npos)
        pos = (pos1 > pos2) ? pos1 : pos2;
    else if (pos1 != std::string::npos)
        pos = pos1;
    else if (pos2 != std::string::npos)
        pos = pos2;

    std::string filename = (pos != std::string::npos) ? path.substr(pos + 1) : path;

    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos)
        filename = filename.substr(0, dot);

    return filename;
}


int main() {
    const std::string key = "ShadowC#PAK";

    auto files = get_files_with_extension("SPRITES", "pak");
    if (files.empty()) {
        std::cout << "No .pak files found in SPRITES directory." << std::endl;
        return 0;
    }

    //double total_encrypt_time = 0.0;
    //double total_load_time = 0.0;
    //double total_get_time = 0.0;
    //double total_load_fast_time = 0.0;
    //double total_get_fast_time = 0.0;
    double total_load_enc_time = 0.0;
    double total_get_enc_time = 0.0;
    double total_load_fast_enc_time = 0.0;
    double total_get_fast_enc_time = 0.0;
    size_t file_count = 0;

    for (const auto& f : files) {
        std::cout << "Processing PAK file: " << get_filename(f) << std::endl;

        //std::string path = path_combine(get_executable_dir(), "SPRITES", f);
		std::string enc_path = path_combine(get_executable_dir(), "ESPRITESCSharp", get_filename_no_ext(f) + ".epak");

        // --- Encrypt file ---
        //double encrypt_time = 0.0;
        //try {
        //    auto t_enc1 = std::chrono::high_resolution_clock::now();
        //    PAKLib::encrypt_file(path, enc_path, key);
        //    auto t_enc2 = std::chrono::high_resolution_clock::now();
        //    encrypt_time = std::chrono::duration_cast<std::chrono::microseconds>(t_enc2 - t_enc1).count() / 1000.0;
        //}
        //catch (const std::exception& ex) {
        //    std::cerr << "Encryption failed for " << filename << ": " << ex.what() << std::endl;
        //    continue;
        //}

        // --- load ---
        //auto t1 = std::chrono::high_resolution_clock::now();
        //auto pak_file = PAKLib::loadpak(path);
        //auto t2 = std::chrono::high_resolution_clock::now();
        //double load_time = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;

        //// --- load_fast ---
        //auto t3 = std::chrono::high_resolution_clock::now();
        //auto pak_file_fast = PAKLib::loadpak_fast(path);
        //auto t4 = std::chrono::high_resolution_clock::now();
        //double load_fast_time = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count() / 1000.0;

        //// --- get_sprite ---
        //auto t5 = std::chrono::high_resolution_clock::now();
        //auto sprite = PAKLib::get_sprite(path, 0);
        //auto t6 = std::chrono::high_resolution_clock::now();
        //double get_time = std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count() / 1000.0;

        //// --- get_sprite_fast ---
        //auto t7 = std::chrono::high_resolution_clock::now();
        //auto sprite_fast = PAKLib::get_sprite_fast(path, 0);
        //auto t8 = std::chrono::high_resolution_clock::now();
        //double get_fast_time = std::chrono::duration_cast<std::chrono::microseconds>(t8 - t7).count() / 1000.0;

        // --- load_encrypted ---
        auto t9 = std::chrono::high_resolution_clock::now();
        auto pak_file_enc = PAKLib::loadpak_encrypted(enc_path, key);
        auto t10 = std::chrono::high_resolution_clock::now();
        double load_enc_time = std::chrono::duration_cast<std::chrono::microseconds>(t10 - t9).count() / 1000.0;

        // --- load_fast_encrypted ---
        auto t11 = std::chrono::high_resolution_clock::now();
        auto pak_file_fast_enc = PAKLib::loadpak_fast_encrypted(enc_path, key);
        auto t12 = std::chrono::high_resolution_clock::now();
        double load_fast_enc_time = std::chrono::duration_cast<std::chrono::microseconds>(t12 - t11).count() / 1000.0;

        // --- get_sprite_encrypted ---
        auto t13 = std::chrono::high_resolution_clock::now();
        auto sprite_enc = PAKLib::get_sprite_encrypted(enc_path, 0, key);
        auto t14 = std::chrono::high_resolution_clock::now();
        double get_enc_time = std::chrono::duration_cast<std::chrono::microseconds>(t14 - t13).count() / 1000.0;

        // --- get_sprite_fast_encrypted ---
        auto t15 = std::chrono::high_resolution_clock::now();
        auto sprite_fast_enc = PAKLib::get_sprite_fast_encrypted(enc_path, 0, key);
        auto t16 = std::chrono::high_resolution_clock::now();
        double get_fast_enc_time = std::chrono::duration_cast<std::chrono::microseconds>(t16 - t15).count() / 1000.0;

        //std::cout << "encrypt: " << encrypt_time << " ms\n";
        //std::cout << "load: " << load_time << " ms\n";
        //std::cout << "load_fast: " << load_fast_time << " ms\n";
        //std::cout << "get_sprite: " << get_time << " ms\n";
        //std::cout << "get_sprite_fast: " << get_fast_time << " ms\n";
        std::cout << "load_encrypted: " << load_enc_time << " ms\n";
        std::cout << "load_fast_encrypted: " << load_fast_enc_time << " ms\n";
        std::cout << "get_sprite_encrypted: " << get_enc_time << " ms\n";
        std::cout << "get_sprite_fast_encrypted: " << get_fast_enc_time << " ms\n";
        std::cout << "----------------------------------------" << std::endl << std::endl;

        //total_encrypt_time += encrypt_time;
        //total_load_time += load_time;
        //total_load_fast_time += load_fast_time;
        //total_get_time += get_time;
        //total_get_fast_time += get_fast_time;
        total_load_enc_time += load_enc_time;
        total_load_fast_enc_time += load_fast_enc_time;
        total_get_enc_time += get_enc_time;
        total_get_fast_enc_time += get_fast_enc_time;
        ++file_count;
    }

    if (file_count > 0) {
        //double avg_encrypt = total_encrypt_time / file_count;
        //double avg_nonfast = (total_load_time + total_get_time) / (file_count * 2);
        //double avg_fast = (total_load_fast_time + total_get_fast_time) / (file_count * 2);
        double avg_nonfast_enc = (total_load_enc_time + total_get_enc_time) / (file_count * 2);
        double avg_fast_enc = (total_load_fast_enc_time + total_get_fast_enc_time) / (file_count * 2);

        //std::cout << "Average (encryption): " << avg_encrypt << " ms" << std::endl;
        //std::cout << "Average (non-fast): " << avg_nonfast << " ms" << std::endl;
        //std::cout << "Average (fast): " << avg_fast << " ms" << std::endl;
        std::cout << "Average (non-fast encrypted): " << avg_nonfast_enc << " ms" << std::endl;
        std::cout << "Average (fast encrypted): " << avg_fast_enc << " ms" << std::endl;
    }

    return 0;
}
