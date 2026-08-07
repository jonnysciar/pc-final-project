#pragma once

// ============================================================
// HDF5 STREAM WRITER
// Datasets:
//   /weight  shape = (particles)              fixed
//   /step    shape = (frames)                 unlimited
//   /pos     shape = (frames, particles, 2)   unlimited in frame dimension
//   /vel     shape = (frames, particles, 2)   unlimited in frame dimension
//   /screen  shape = (frames, ny, nx)         unlimited in frame dimension
// ============================================================

#include "constants.hpp"
#include "util.hpp"

#include <array>

#ifdef USE_HDF5

#include <H5Cpp.h>

class H5StreamWriter {
public:
    H5StreamWriter(
        const std::string& name,
        std::size_t np,
        std::size_t nx,
        std::size_t ny,
        std::size_t chunkFrames = 64,
        std::size_t screenTileY = 256,
        std::size_t screenTileX = 256
    )
        : file_(name, H5F_ACC_TRUNC),
          np_(np),
          nx_(nx),
          ny_(ny),
          chunkFrames_(chunkFrames),
          capacity_(chunkFrames),
          currentFrame_(0),
          Pbuf_(safeMul(np, 2, "position buffer size")),
          Vbuf_(safeMul(np, 2, "velocity buffer size")),
          closed_(false)
    {
        if (np_ == 0) {
            throw std::invalid_argument("H5StreamWriter: np must be > 0");
        }
        if (nx_ == 0 || ny_ == 0) {
            throw std::invalid_argument("H5StreamWriter: nx and ny must be > 0");
        }
        if (chunkFrames_ == 0) {
            throw std::invalid_argument("H5StreamWriter: chunkFrames must be > 0");
        }
        if (screenTileY == 0 || screenTileX == 0) {
            throw std::invalid_argument("H5StreamWriter: screen tile sizes must be > 0");
        }

        createParticleDatasets();
        createScreenDataset(screenTileY, screenTileX);
        createStepDataset();
        createWeightDataset();

        extendDatasets(capacity_);
    }

    ~H5StreamWriter() noexcept {
        try {
            close();
        } catch (...) {
        }
    }

    H5StreamWriter(const H5StreamWriter&) = delete;
    H5StreamWriter& operator=(const H5StreamWriter&) = delete;

    void writeMetadata(
        const std::string& inputFile,
        const Config& cfg,
        const Grid& gen,
        const Grid& screenGrid
    ) {
        H5::Group root = file_.openGroup("/");

        writeStringAttribute(root, "application", "Particle System Solver");
        writeStringAttribute(root, "format_version", "2.0");
        writeStringAttribute(root, "input_file", inputFile);
        writeStringAttribute(root, "screen_dataset_note", "For visualization/debugging; not recommended for strict grading.");

        writeULLAttribute(root, "particles", static_cast<unsigned long long>(np_));

        writeULLAttribute(root, "generating_grid_nx", static_cast<unsigned long long>(gen.nx));
        writeULLAttribute(root, "generating_grid_ny", static_cast<unsigned long long>(gen.ny));
        writeDoubleAttribute(root, "generating_grid_xs", gen.xs);
        writeDoubleAttribute(root, "generating_grid_xe", gen.xe);
        writeDoubleAttribute(root, "generating_grid_ys", gen.ys);
        writeDoubleAttribute(root, "generating_grid_ye", gen.ye);

        writeULLAttribute(root, "screen_grid_nx", static_cast<unsigned long long>(screenGrid.nx));
        writeULLAttribute(root, "screen_grid_ny", static_cast<unsigned long long>(screenGrid.ny));
        writeDoubleAttribute(root, "screen_grid_xs", screenGrid.xs);
        writeDoubleAttribute(root, "screen_grid_xe", screenGrid.xe);
        writeDoubleAttribute(root, "screen_grid_ys", screenGrid.ys);
        writeDoubleAttribute(root, "screen_grid_ye", screenGrid.ye);

        writeULLAttribute(root, "max_iters", static_cast<unsigned long long>(cfg.maxIters));
        writeULLAttribute(root, "max_steps", static_cast<unsigned long long>(cfg.maxSteps));
        writeULLAttribute(root, "output_every", static_cast<unsigned long long>(cfg.outputEvery));

        writeDoubleAttribute(root, "dt", cfg.dt);
        writeDoubleAttribute(root, "kForce", kForce);
        writeDoubleAttribute(root, "eps", eps);
        writeDoubleAttribute(root, "eps2", eps2);

        root.close();
    }

    void writeWeights(const Particles& P) {
        if (closed_) {
            throw std::runtime_error("H5StreamWriter: writeWeights after close");
        }
        if (P.n != np_) {
            throw std::runtime_error("H5StreamWriter: particle size mismatch in writeWeights");
        }
        weight_.write(P.w.data(), H5::PredType::NATIVE_DOUBLE);
    }

    void writeFrame(std::size_t stepNumber, const Particles& P, const Grid& screen) {
        if (closed_) {
            throw std::runtime_error("H5StreamWriter: write after close");
        }
        if (P.n != np_) {
            throw std::runtime_error("H5StreamWriter: particle size mismatch");
        }
        if (screen.nx != nx_ || screen.ny != ny_) {
            throw std::runtime_error("H5StreamWriter: screen size mismatch");
        }
        if (screen.values.size() != safeGridSize(nx_, ny_)) {
            throw std::runtime_error("H5StreamWriter: screen storage size mismatch");
        }

        if (currentFrame_ >= capacity_) {
            capacity_ += chunkFrames_;
            extendDatasets(capacity_);
        }

        for (std::size_t i = 0; i < np_; ++i) {
            Pbuf_[2 * i]     = P.x[i];
            Pbuf_[2 * i + 1] = P.y[i];

            Vbuf_[2 * i]     = P.vx[i];
            Vbuf_[2 * i + 1] = P.vy[i];
        }

        writeDoubleFrame(pos_, Pbuf_.data(), currentFrame_, np_, 2);
        writeDoubleFrame(vel_, Vbuf_.data(), currentFrame_, np_, 2);
        writeScreenFrame(screen_, screen.values.data(), currentFrame_, ny_, nx_);
        writeStep(step_, static_cast<long long>(stepNumber), currentFrame_);

        ++currentFrame_;
    }

    hsize_t framesWritten() const noexcept {
        return currentFrame_;
    }

    void close() {
        if (closed_) {
            return;
        }

        shrinkToFit();
        file_.flush(H5F_SCOPE_GLOBAL);

        pos_.close();
        vel_.close();
        screen_.close();
        step_.close();
        weight_.close();
        file_.close();

        closed_ = true;
    }

private:
    void createParticleDatasets() {
        hsize_t dims[3] = {0, static_cast<hsize_t>(np_), 2};
        hsize_t maxdims[3] = {H5S_UNLIMITED, static_cast<hsize_t>(np_), 2};

        H5::DataSpace space(3, dims, maxdims);
        H5::DSetCreatPropList prop;

        hsize_t chunk[3] = {1, static_cast<hsize_t>(np_), 2};
        prop.setChunk(3, chunk);

        pos_ = file_.createDataSet("/pos", H5::PredType::NATIVE_DOUBLE, space, prop);
        vel_ = file_.createDataSet("/vel", H5::PredType::NATIVE_DOUBLE, space, prop);
    }

    void createScreenDataset(std::size_t screenTileY, std::size_t screenTileX) {
        const hsize_t screenChunkY = static_cast<hsize_t>(std::min<std::size_t>(ny_, screenTileY));
        const hsize_t screenChunkX = static_cast<hsize_t>(std::min<std::size_t>(nx_, screenTileX));

        hsize_t dims[3] = {0, static_cast<hsize_t>(ny_), static_cast<hsize_t>(nx_)};
        hsize_t maxdims[3] = {H5S_UNLIMITED, static_cast<hsize_t>(ny_), static_cast<hsize_t>(nx_)};

        H5::DataSpace space(3, dims, maxdims);
        H5::DSetCreatPropList prop;

        hsize_t chunk[3] = {1, screenChunkY, screenChunkX};
        prop.setChunk(3, chunk);

        screen_ = file_.createDataSet("/screen", H5::PredType::NATIVE_ULLONG, space, prop);
    }

    void createStepDataset() {
        hsize_t dims[1] = {0};
        hsize_t maxdims[1] = {H5S_UNLIMITED};

        H5::DataSpace space(1, dims, maxdims);
        H5::DSetCreatPropList prop;

        hsize_t chunk[1] = {static_cast<hsize_t>(chunkFrames_)};
        prop.setChunk(1, chunk);

        step_ = file_.createDataSet("/step", H5::PredType::NATIVE_LLONG, space, prop);
    }

    void createWeightDataset() {
        hsize_t dims[1] = {static_cast<hsize_t>(np_)};
        H5::DataSpace space(1, dims);

        weight_ = file_.createDataSet("/weight", H5::PredType::NATIVE_DOUBLE, space);
    }

    void extendDatasets(hsize_t newSize) {
        {
            std::array<hsize_t, 3> size = {newSize, static_cast<hsize_t>(np_), 2};
            pos_.extend(size.data());
            vel_.extend(size.data());
        }
        {
            std::array<hsize_t, 3> size = {newSize, static_cast<hsize_t>(ny_), static_cast<hsize_t>(nx_)};
            screen_.extend(size.data());
        }
        {
            std::array<hsize_t, 1> size = {newSize};
            step_.extend(size.data());
        }
    }

    void shrinkToFit() {
        extendDatasets(currentFrame_);
    }

    static void writeStringAttribute(H5::H5Object& object, const std::string& name, const std::string& value) {
        H5::DataSpace space(H5S_SCALAR);
        H5::StrType type(H5::PredType::C_S1, value.size() + 1);
        H5::Attribute attr = object.createAttribute(name, type, space);
        attr.write(type, value.c_str());
        attr.close();
    }

    static void writeULLAttribute(H5::H5Object& object, const std::string& name, unsigned long long value) {
        H5::DataSpace space(H5S_SCALAR);
        H5::Attribute attr = object.createAttribute(name, H5::PredType::NATIVE_ULLONG, space);
        attr.write(H5::PredType::NATIVE_ULLONG, &value);
        attr.close();
    }

    static void writeDoubleAttribute(H5::H5Object& object, const std::string& name, double value) {
        H5::DataSpace space(H5S_SCALAR);
        H5::Attribute attr = object.createAttribute(name, H5::PredType::NATIVE_DOUBLE, space);
        attr.write(H5::PredType::NATIVE_DOUBLE, &value);
        attr.close();
    }

    static void writeDoubleFrame(H5::DataSet& dataset, const double* data, hsize_t frame, hsize_t dim1, hsize_t dim2) {
        H5::DataSpace filespace = dataset.getSpace();

        hsize_t start[3] = {frame, 0, 0};
        hsize_t count[3] = {1, dim1, dim2};

        filespace.selectHyperslab(H5S_SELECT_SET, count, start);
        H5::DataSpace memspace(3, count);

        dataset.write(data, H5::PredType::NATIVE_DOUBLE, memspace, filespace);
    }

    static void writeScreenFrame(H5::DataSet& dataset, const unsigned long long* data, hsize_t frame, hsize_t ny, hsize_t nx) {
        H5::DataSpace filespace = dataset.getSpace();

        hsize_t start[3] = {frame, 0, 0};
        hsize_t count[3] = {1, ny, nx};

        filespace.selectHyperslab(H5S_SELECT_SET, count, start);
        H5::DataSpace memspace(3, count);

        dataset.write(data, H5::PredType::NATIVE_ULLONG, memspace, filespace);
    }

    static void writeStep(H5::DataSet& dataset, long long stepValue, hsize_t frame) {
        H5::DataSpace filespace = dataset.getSpace();

        hsize_t start[1] = {frame};
        hsize_t count[1] = {1};

        filespace.selectHyperslab(H5S_SELECT_SET, count, start);
        H5::DataSpace memspace(1, count);

        dataset.write(&stepValue, H5::PredType::NATIVE_LLONG, memspace, filespace);
    }

private:
    H5::H5File file_;

    H5::DataSet pos_;
    H5::DataSet vel_;
    H5::DataSet screen_;
    H5::DataSet step_;
    H5::DataSet weight_;

    std::size_t np_{};
    std::size_t nx_{};
    std::size_t ny_{};
    std::size_t chunkFrames_{};

    hsize_t capacity_{};
    hsize_t currentFrame_{};

    std::vector<double> Pbuf_;
    std::vector<double> Vbuf_;

    bool closed_{false};
};

#else

class H5StreamWriter {
public:
    H5StreamWriter(
        const std::string&,
        std::size_t,
        std::size_t,
        std::size_t,
        std::size_t = 64,
        std::size_t = 256,
        std::size_t = 256
    ) {
        throw std::runtime_error(
            "This executable was built without HDF5 support. "
            "Use 'none' as the HDF5 output argument, or rebuild with -DUSE_HDF5."
        );
    }

    H5StreamWriter(const H5StreamWriter&) = delete;
    H5StreamWriter& operator=(const H5StreamWriter&) = delete;

    void writeMetadata(const std::string&, const Config&, const Grid&, const Grid&) {}
    void writeWeights(const Particles&) {}
    void writeFrame(std::size_t, const Particles&, const Grid&) {}

    std::size_t framesWritten() const noexcept { return 0; }
    void close() {}
};

#endif


