#include "kimera_topology_test/test_fixtures.h"

#include <gtest/gtest.h>

#include <kimera_topology/gvd_integrator.h>

namespace kimera {
namespace topology {

using test_helpers::SingleBlockTestFixture;
using test_helpers::TestFixture2d;
using voxblox::VoxelIndex;

class ParentTestFixture : public SingleBlockTestFixture {
 public:
  ParentTestFixture() : SingleBlockTestFixture() {}
  virtual ~ParentTestFixture() = default;

  virtual void SetUp() override {
    voxels_per_side = 8;
    truncation_distance = 0.2;
    SingleBlockTestFixture::SetUp();

    for (int r = 1; r < voxels_per_side; ++r) {
      for (int c = 1; c < voxels_per_side; ++c) {
        setTsdfVoxel(1, r, c, 0.1);
        setTsdfVoxel(r, 1, c, 0.1);
        setTsdfVoxel(r, c, 1, 0.1);
      }
    }
  }
};

struct GvdResult {
  GvdResult(int rows, int cols) {
    distances = Eigen::MatrixXd(rows, cols);
    is_voronoi = Eigen::MatrixXd(rows, cols);
  }

  Eigen::MatrixXd distances;
  Eigen::MatrixXd is_voronoi;
};

std::ostream& operator<<(std::ostream& out, const GvdResult& result) {
  out << std::endl;
  for (int r = 0; r < result.distances.rows(); ++r) {
    for (int c = 0; c < result.distances.cols(); ++c) {
      out << "+-------";
    }
    out << "+" << std::endl;

    out << "|";
    for (int c = 0; c < result.distances.cols(); ++c) {
      out << (result.is_voronoi(r, c) == 1.0 ? " @" : "  ");
      out << std::setw(5) << std::setprecision(2) << std::fixed << std::setfill(' ')
          << result.distances(r, c);
      out << "|";
    }
    out << std::endl;
  }

  for (int c = 0; c < result.distances.cols(); ++c) {
    out << "+-------";
  }
  out << "+";

  out << std::endl;

  return out;
}

TEST_F(TestFixture2d, OccupancyIntegrationCorrect) {
  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < 4; ++y) {
      setTsdfVoxel(x, y, 3.0 * voxel_size);
    }
  }

  setTsdfVoxel(0, 0, 0.0);
  setTsdfVoxel(0, 1, 0.0);
  setTsdfVoxel(0, 2, 0.0);
  setTsdfVoxel(0, 3, 0.0);
  setTsdfVoxel(3, 2, 0.0);

  gvd_config.min_diff_m = 0.0;
  gvd_config.min_distance_m = voxel_size;
  gvd_config.max_distance_m = 50.0;
  gvd_config.voronoi_min_distance_m = 1.0;
  gvd_config.parent_derived_distance = false;

  GvdIntegrator gvd_integrator(gvd_config, tsdf_layer, gvd_layer, mesh_layer);
  gvd_integrator.updateFromTsdfLayer(true);

  GvdResult result(4, 8);
  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < 4; ++y) {
      const auto& voxel = getGvdVoxel(x, y);
      result.distances(y, x) = voxel.observed ? voxel.distance : -1.0;
      result.is_voronoi(y, x) = voxel.is_voronoi ? 1.0 : 0.0;
    }
  }

  VLOG(1) << "Result: " << result;
}

TEST_F(SingleBlockTestFixture, PlaneCorrect) {
  GvdIntegrator gvd_integrator(gvd_config, tsdf_layer, gvd_layer, mesh_layer);
  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < voxels_per_side; ++y) {
      for (int z = 0; z < voxels_per_side; ++z) {
        const bool is_edge = (x == 0);
        setTsdfVoxel(x, y, z, is_edge ? 0.0 : truncation_distance);
      }
    }
  }

  gvd_integrator.updateFromTsdfLayer(true);

  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < voxels_per_side; ++y) {
      for (int z = 0; z < voxels_per_side; ++z) {
        const auto& voxel = getGvdVoxel(x, y, z);

        double expected_distance = x * truncation_distance;

        EXPECT_NEAR(expected_distance, voxel.distance, 1.0e-6);
        EXPECT_TRUE(!voxel.is_voronoi || !voxel.fixed);
        EXPECT_TRUE(voxel.on_surface || voxel.has_parent);
        EXPECT_FALSE(voxel.is_voronoi)
            << voxel << " @ (" << x << ", " << y << ", " << z << ")";
      }
    }
  }
}

TEST_F(SingleBlockTestFixture, LCorrect) {
  GvdIntegrator gvd_integrator(gvd_config, tsdf_layer, gvd_layer, mesh_layer);
  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < voxels_per_side; ++y) {
      for (int z = 0; z < voxels_per_side; ++z) {
        const bool is_edge = (x == 0) || (y == 0);
        setTsdfVoxel(x, y, z, is_edge ? 0.0 : truncation_distance);
      }
    }
  }

  gvd_integrator.updateFromTsdfLayer(true);

  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < voxels_per_side; ++y) {
      for (int z = 0; z < voxels_per_side; ++z) {
        const auto& voxel = getGvdVoxel(x, y, z);

        double expected_distance = std::min(x, y) * truncation_distance;

        EXPECT_NEAR(expected_distance, voxel.distance, 1.0e-6);
        EXPECT_TRUE(!voxel.is_voronoi || !voxel.fixed);
        EXPECT_TRUE(voxel.on_surface || voxel.has_parent);

        // 8-connected diagonal plane should be voronoi
        EXPECT_EQ(std::abs(x - y) <= 1 && x >= 2 && y >= 2, voxel.is_voronoi)
            << voxel << " @ (" << x << ", " << y << ", " << z << ")";
      }
    }
  }
}

TEST_F(SingleBlockTestFixture, CornerCorrect) {
  GvdIntegrator gvd_integrator(gvd_config, tsdf_layer, gvd_layer, mesh_layer);
  gvd_integrator.updateFromTsdfLayer(true);

  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < voxels_per_side; ++y) {
      for (int z = 0; z < voxels_per_side; ++z) {
        const auto& voxel = getGvdVoxel(x, y, z);

        double expected_distance = std::min(x, std::min(y, z)) * truncation_distance;

        EXPECT_NEAR(expected_distance, voxel.distance, 1.0e-6);
        EXPECT_TRUE(!voxel.is_voronoi || !voxel.fixed);
        EXPECT_TRUE(voxel.on_surface || voxel.has_parent)
            << voxel << " @ (" << x << ", " << y << ", " << z << ")";

        // upper 2x2 should all be voronoi
        EXPECT_EQ(x >= 2 && y >= 2 && z >= 2, voxel.is_voronoi)
            << voxel << " @ (" << x << ", " << y << ", " << z << ")";
      }
    }
  }
}

TEST_F(ParentTestFixture, ParentsCorrect) {
  GvdIntegrator gvd_integrator(gvd_config, tsdf_layer, gvd_layer, mesh_layer);
  gvd_integrator.updateFromTsdfLayer(true);

  for (int x = 0; x < voxels_per_side; ++x) {
    for (int y = 0; y < voxels_per_side; ++y) {
      for (int z = 0; z < voxels_per_side; ++z) {
        const auto& voxel = getGvdVoxel(x, y, z);

        if (x == 0 || y == 0 || z == 0) {
          EXPECT_TRUE(voxel.on_surface)
              << voxel << " @ (" << x << ", " << y << ", " << z << ")";
          continue;
        }

        // in general, it's hard to determine what tie-breaking rules are correct
        // (it depends on wavefront traversal order). We allow for multiple different
        // parents instead of trying to predict the wavefront traversal order
        if (x == y && x == z) {
          // at least two parent coordinates will be equal, and the other will be 0
          // this means that the product of the coordinates will be 0
          // the other two checks follow from x + ? = 2 * x <-> ? = x
          uint64_t total = voxel.parent[0] + voxel.parent[1] + voxel.parent[2];
          uint64_t product = voxel.parent[0] * voxel.parent[1] * voxel.parent[2];
          EXPECT_EQ(0u, product);
          EXPECT_EQ(2u * static_cast<uint64_t>(x), total);
          EXPECT_TRUE(voxel.parent[0] == x || voxel.parent[1] == y || voxel.parent[2] == z);
        } else if (x == y && z > x) {
            EXPECT_EQ(z, voxel.parent[2]);
            EXPECT_TRUE(voxel.parent[0] == 0u || voxel.parent[1] == 0u);
            EXPECT_TRUE(voxel.parent[0] == x || voxel.parent[1] == y);
        } else if (x == z && y > x) {
          EXPECT_EQ(y, voxel.parent[1]);
          EXPECT_TRUE(voxel.parent[0] == 0u || voxel.parent[2] == 0u);
          EXPECT_TRUE(voxel.parent[0] == x || voxel.parent[2] == z);
        } else if (y == z && x > z) {
          EXPECT_EQ(x, voxel.parent[0]);
          EXPECT_TRUE(voxel.parent[1] == 0u || voxel.parent[2] == 0u);
          EXPECT_TRUE(voxel.parent[1] == y || voxel.parent[2] == z);
        } else {
          GlobalIndex expected_parent;
          if (x < y && x < z) {
            expected_parent << 0, y, z;
          } else if (y < x && y < z) {
            expected_parent << x, 0, z;
          } else {
            expected_parent << x, y, 0;
          }

          EXPECT_EQ(expected_parent, Eigen::Map<const GlobalIndex>(voxel.parent))
              << voxel << " @ (" << x << ", " << y << ", " << z << ")"
              << ",  expected parent: " << expected_parent.transpose();
        }
      }
    }
  }
}

}  // namespace topology
}  // namespace kimera
