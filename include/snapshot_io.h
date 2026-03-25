#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>

//=============================================================
//  SnapshotIO 
//
//  File format:
//      Lx  Ly  N              (header)
//      x   y   r   type       (per particle, type is optional on read)
//      ...
//
//  Lines starting with '#' are treated as comments and skipped.
//=============================================================
class SnapshotIO
{
public:
    // A single particle record (pure data, no simulation state)
    struct Record
    {
        double x  = 0;
        double y  = 0;
        double radius = 0;
        int    type   = 0;
    };

    // A complete snapshot (header + particles)
    struct Snapshot
    {
        double lx = 0;
        double ly = 0;
        std::vector<Record> particles;
    };

    // ---- Load ----
    // Read a snapshot file.  Returns the snapshot.
    // If type is absent on a line, defaults to 0.
    static Snapshot load(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "SnapshotIO: cannot open " << filename << "\n";
            std::exit(3);
        }

        Snapshot snap;
        std::string line;

        // Read first non-comment line → header
        while (std::getline(file, line))
            if (!line.empty() && line[0] != '#') break;

        // Detect format by number of tokens in header:
        //   "Lx Ly N"   (3 tokens) → classic format: N given, particles = x y r [type]
        //   "Lx Ly"     (2 tokens) → compact format: N inferred, particles = x y r
        {
            std::istringstream iss(line);
            double a, b; int n = 0;
            if (!(iss >> a >> b))
            {
                std::cerr << "SnapshotIO: header parse error (expected: Lx Ly [N])\n";
                std::exit(3);
            }
            snap.lx = a; snap.ly = b;
            bool hasN = (bool)(iss >> n);

            if (hasN)
            {
                // ---- Classic format: Lx Ly N ----
                snap.particles.resize(n);
                for (int i = 0; i < n; ++i)
                {
                    while (std::getline(file, line))
                        if (!line.empty() && line[0] != '#') break;

                    std::istringstream ps(line);
                    Record& r = snap.particles[i];
                    if (!(ps >> r.x >> r.y >> r.radius))
                    {
                        std::cerr << "SnapshotIO: particle " << i
                                  << " parse error.  Line: " << line << "\n";
                        std::exit(3);
                    }
                    if (!(ps >> r.type)) r.type = 0;
                }
            }
            else
            {
                // ---- Compact format: Lx Ly (no N), particles = x y r ----
                // type 按半径自动分类：半径相同 → 相同 type，按半径从小到大编号 0,1,2,...
                while (std::getline(file, line))
                {
                    if (line.empty() || line[0] == '#') continue;
                    std::istringstream ps(line);
                    Record r;
                    if (!(ps >> r.x >> r.y >> r.radius)) continue;
                    r.type = 0;
                    snap.particles.push_back(r);
                }

                // 收集所有不同半径，排序后作为 type 映射表
                // 用相对容差 1e-6 合并浮点上几乎相等的半径
                std::vector<double> radii;
                for (const auto& p : snap.particles)
                    radii.push_back(p.radius);
                std::sort(radii.begin(), radii.end());
                {
                    std::vector<double> uniq;
                    for (double r : radii)
                        if (uniq.empty() || std::abs(r - uniq.back()) > 1e-6 * uniq.back())
                            uniq.push_back(r);
                    radii = std::move(uniq);
                }

                if (radii.size() > 1)
                {
                    for (auto& p : snap.particles)
                    {
                        // 找最近匹配的半径档
                        int idx = 0;
                        double best = std::abs(p.radius - radii[0]);
                        for (int k = 1; k < (int)radii.size(); k++)
                        {
                            double d = std::abs(p.radius - radii[k]);
                            if (d < best) { best = d; idx = k; }
                        }
                        p.type = idx;
                    }
                    std::cerr << "SnapshotIO: detected " << radii.size()
                              << " radius classes → types 0.."
                              << radii.size() - 1 << "\n";
                }
            }
        }
        return snap;
    }

    // ---- Save snapshot to file ----
    static void save(const std::string& filename,
                     double lx, double ly,
                     const std::vector<Record>& records)
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "SnapshotIO: cannot open " << filename << " for writing\n";
            return;
        }
        file << std::fixed << std::setprecision(12);
        file << lx << " " << ly << " " << records.size() << "\n";
        for (const auto& r : records)
        {
            file << r.x << " " << r.y << " " << r.radius << " " << r.type << "\n";
        }
    }

    // ---- Append a movie frame ----
    static void appendFrame(const std::string& filename,
                            double lx, double ly,
                            const std::vector<Record>& records,
                            bool firstFrame)
    {
        std::ofstream file;
        if (firstFrame) file.open(filename, std::ios::out);
        else            file.open(filename, std::ios::app);
        if (!file.is_open()) return;

        file << std::fixed << std::setprecision(12);
        file << lx << " " << ly << " " << records.size() << "\n";
        for (const auto& r : records)
        {
            file << r.x << " " << r.y << " " << r.radius << " " << r.type << "\n";
        }
    }

    // ---- Append one line of pressure data ----
    static void appendPressure(const std::string& filename,
                               double simtime, double pressure,
                               bool firstWrite)
    {
        std::ofstream file;
        if (firstWrite) file.open(filename, std::ios::out);
        else            file.open(filename, std::ios::app);
        if (!file.is_open()) return;
        file << std::fixed << std::setprecision(6);
        file << simtime << " " << pressure << "\n";
    }
};
