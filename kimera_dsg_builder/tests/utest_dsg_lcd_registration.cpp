#include <kimera_dsg_builder/dsg_lcd_registration.h>

#include <gtest/gtest.h>

namespace kimera {
namespace lcd {

using incremental::SharedDsgInfo;

struct LayerRegistrationTests : public ::testing::Test {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  LayerRegistrationTests() : testing::Test() {}

  virtual ~LayerRegistrationTests() = default;

  virtual void SetUp() override {
    dest_R_src << 0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0;
    dest_t_src << 1.0, 2.0, 3.0;

    src_points = 5.0 * Eigen::MatrixXd::Random(3, 40);
    dest_points = dest_R_src * src_points;
    dest_points.colwise() += dest_t_src;

    src_layer.reset(new IsolatedSceneGraphLayer(1));
    dest_layer.reset(new IsolatedSceneGraphLayer(1));

    for (int i = 0; i < src_points.cols(); ++i) {
      auto src_attrs = std::make_unique<SemanticNodeAttributes>();
      src_attrs->position = src_points.col(i);
      src_layer->emplaceNode(i, std::move(src_attrs));

      auto dest_attrs = std::make_unique<SemanticNodeAttributes>();
      dest_attrs->position = dest_points.col(i);
      dest_layer->emplaceNode(i, std::move(dest_attrs));

      node_ids.push_back(i);
    }
  }

  Eigen::MatrixXd src_points;
  Eigen::MatrixXd dest_points;

  Eigen::Matrix3d dest_R_src;
  Eigen::Vector3d dest_t_src;

  std::list<NodeId> node_ids;
  std::unique_ptr<IsolatedSceneGraphLayer> src_layer;
  std::unique_ptr<IsolatedSceneGraphLayer> dest_layer;

  LayerRegistrationConfig reg_config;
};

struct DsgRegistrationTests : public ::testing::Test {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  DsgRegistrationTests() : testing::Test() {}

  ~DsgRegistrationTests() = default;

  virtual void SetUp() override {
    std::map<LayerId, char> layer_map = {{KimeraDsgLayers::PLACES, 'p'},
                                         {KimeraDsgLayers::OBJECTS, 'O'},
                                         {KimeraDsgLayers::ROOMS, 'R'}};

    dsg.reset(new SharedDsgInfo(layer_map, KimeraDsgLayers::MESH));

    using namespace std::chrono_literals;

    const double angle = M_PI / 6;
    dest_R_src << std::cos(angle), -std::sin(angle), 0.0, std::sin(angle),
        std::cos(angle), 0.0, 0.0, 0.0, 1.0;
    dest_t_src << 0.1, 0.2, 0.3;

    src_points = 5.0 * Eigen::MatrixXd::Random(3, 30);

    CHECK(dsg->graph->hasLayer(KimeraDsgLayers::OBJECTS));

    for (int i = 0; i < src_points.cols(); ++i) {
      SemanticNodeAttributes attrs;
      attrs.position = src_points.col(i);
      attrs.semantic_label = i;
      CHECK(dsg->graph->emplaceNode(KimeraDsgLayers::PLACES,
                                    NodeSymbol('p', i),
                                    std::make_unique<SemanticNodeAttributes>(attrs)));
      CHECK(dsg->graph->emplaceNode(KimeraDsgLayers::OBJECTS,
                                    NodeSymbol('O', i),
                                    std::make_unique<SemanticNodeAttributes>(attrs)));
      dsg->graph->insertEdge(NodeSymbol('p', i), NodeSymbol('O', i));

      attrs.position = dest_R_src * src_points.col(i) + dest_t_src;
      CHECK(dsg->graph->emplaceNode(KimeraDsgLayers::PLACES,
                                    NodeSymbol('p', i + src_points.cols()),
                                    std::make_unique<SemanticNodeAttributes>(attrs)));
      CHECK(dsg->graph->emplaceNode(KimeraDsgLayers::OBJECTS,
                                    NodeSymbol('O', i + src_points.cols()),
                                    std::make_unique<SemanticNodeAttributes>(attrs)));
      dsg->graph->insertEdge(NodeSymbol('p', i + src_points.cols()),
                             NodeSymbol('O', i + src_points.cols()));
    }

    CHECK(dsg->graph->hasNode(NodeSymbol('O', 40)));

    Eigen::Quaterniond world_q_body1(std::cos(M_PI / 8), std::sin(M_PI / 8), 0.0, 0.0);
    Eigen::Vector3d world_t_body1(-1.0, 0.2, 0.5);
    dsg->graph->emplaceDynamicNode(
        KimeraDsgLayers::AGENTS,
        'a',
        10ns,
        std::make_unique<AgentNodeAttributes>(world_q_body1, world_t_body1, NodeId(0)));

    Eigen::Quaterniond world_q_body2(std::cos(M_PI / 8), 0.0, std::sin(M_PI / 8), 0.0);
    Eigen::Vector3d world_t_body2(5.0, -0.3, 2.1);

    Eigen::Quaterniond dest_q_body2 = Eigen::Quaterniond(dest_R_src) * world_q_body2;
    Eigen::Vector3d dest_t_body2 = dest_R_src * world_t_body2 + dest_t_src;
    dsg->graph->emplaceDynamicNode(
        KimeraDsgLayers::AGENTS,
        'a',
        20ns,
        std::make_unique<AgentNodeAttributes>(dest_q_body2, dest_t_body2, NodeId(0)));

    dsg->graph->insertEdge(NodeSymbol('p', 0), NodeSymbol('a', 0));
    dsg->graph->insertEdge(NodeSymbol('p', src_points.cols()), NodeSymbol('a', 1));

    gtsam::Pose3 world_T_to(gtsam::Rot3(world_q_body1), world_t_body1);
    gtsam::Pose3 world_T_from(gtsam::Rot3(world_q_body2), world_t_body2);
    to_T_from = world_T_to.between(world_T_from);
  }

  Eigen::MatrixXd src_points;

  Eigen::Matrix3d dest_R_src;
  Eigen::Vector3d dest_t_src;

  gtsam::Pose3 to_T_from;

  SharedDsgInfo::Ptr dsg;
  LayerRegistrationConfig reg_config;
};

TEST_F(LayerRegistrationTests, TestCorrectCorrespondenceRegistration) {
  teaser::RobustRegistrationSolver::Params params;
  params.estimate_scaling = false;
  teaser::RobustRegistrationSolver solver(params);

  LayerRegistrationProblem problem;
  problem.src_nodes = node_ids;
  problem.dest_nodes = node_ids;
  problem.dest_layer = dest_layer.get();

  auto solution =
      registerDsgLayer(reg_config,
                       solver,
                       problem,
                       *src_layer,
                       [](const SceneGraphNode& src, const SceneGraphNode& dest) {
                         return src.id == dest.id;
                       });

  ASSERT_TRUE(solution.valid);
  EXPECT_NEAR(dest_t_src.x(), solution.dest_T_src.translation().x(), 1.0e-4);
  EXPECT_NEAR(dest_t_src.y(), solution.dest_T_src.translation().y(), 1.0e-4);
  EXPECT_NEAR(dest_t_src.z(), solution.dest_T_src.translation().z(), 1.0e-4);
  gtsam::Rot3 src_R_dest_gt(dest_R_src.transpose());
  double rot_error =
      gtsam::Rot3::Logmap(src_R_dest_gt * solution.dest_T_src.rotation()).norm();
  EXPECT_NEAR(0.0, rot_error, 1.0e-4);

  EXPECT_EQ(solution.inliers.size(), node_ids.size());
  for (const auto& correspondence : solution.inliers) {
    EXPECT_EQ(correspondence.first, correspondence.second);
  }
}

TEST_F(LayerRegistrationTests, TestPairwiseRegistration) {
  teaser::RobustRegistrationSolver::Params params;
  params.estimate_scaling = false;
  teaser::RobustRegistrationSolver solver(params);

  LayerRegistrationProblem problem;
  problem.src_nodes = node_ids;
  problem.dest_nodes = node_ids;
  problem.dest_layer = dest_layer.get();

  auto solution = registerDsgLayerPairwise(reg_config, solver, problem, *src_layer);

  ASSERT_TRUE(solution.valid);
  EXPECT_NEAR(dest_t_src.x(), solution.dest_T_src.translation().x(), 1.0e-4);
  EXPECT_NEAR(dest_t_src.y(), solution.dest_T_src.translation().y(), 1.0e-4);
  EXPECT_NEAR(dest_t_src.z(), solution.dest_T_src.translation().z(), 1.0e-4);
  gtsam::Rot3 src_R_dest_gt(dest_R_src.transpose());
  double rot_error =
      gtsam::Rot3::Logmap(src_R_dest_gt * solution.dest_T_src.rotation()).norm();
  EXPECT_NEAR(0.0, rot_error, 1.0e-4);

  EXPECT_EQ(solution.inliers.size(), node_ids.size());
  for (const auto& correspondence : solution.inliers) {
    EXPECT_EQ(correspondence.first, correspondence.second);
  }
}

TEST_F(LayerRegistrationTests, TestSemanticRegistration) {
  size_t count = 0;
  for (const auto& id_node_pair : src_layer->nodes()) {
    uint8_t label = (count > (src_layer->numNodes() / 2)) ? 0 : 1;
    id_node_pair.second->attributes<SemanticNodeAttributes>().semantic_label = label;
    dest_layer->getNode(id_node_pair.first)
        .value()
        .get()
        .attributes<SemanticNodeAttributes>()
        .semantic_label = label;
    count++;
  }

  teaser::RobustRegistrationSolver::Params params;
  params.estimate_scaling = false;
  teaser::RobustRegistrationSolver solver(params);

  LayerRegistrationProblem problem;
  problem.src_nodes = node_ids;
  problem.dest_nodes = node_ids;
  problem.dest_layer = dest_layer.get();

  auto solution = registerDsgLayerSemantic(reg_config, solver, problem, *src_layer);

  ASSERT_TRUE(solution.valid);
  EXPECT_NEAR(dest_t_src.x(), solution.dest_T_src.translation().x(), 1.0e-4);
  EXPECT_NEAR(dest_t_src.y(), solution.dest_T_src.translation().y(), 1.0e-4);
  EXPECT_NEAR(dest_t_src.z(), solution.dest_T_src.translation().z(), 1.0e-4);
  gtsam::Rot3 src_R_dest_gt(dest_R_src.transpose());
  double rot_error =
      gtsam::Rot3::Logmap(src_R_dest_gt * solution.dest_T_src.rotation()).norm();
  EXPECT_NEAR(0.0, rot_error, 1.0e-4);

  EXPECT_EQ(solution.inliers.size(), node_ids.size());
  for (const auto& correspondence : solution.inliers) {
    EXPECT_EQ(correspondence.first, correspondence.second);
  }
}

TEST_F(DsgRegistrationTests, TestFullObjectRegistration) {
  LayerSearchResults match;
  for (int i = 0; i < src_points.cols(); ++i) {
    match.query_nodes.insert(NodeSymbol('O', i + src_points.cols()));
    match.match_nodes.insert(NodeSymbol('O', i));
  }

  match.query_root = NodeSymbol('p', src_points.cols());
  match.match_root = NodeSymbol('p', 0);

  teaser::RobustRegistrationSolver::Params params;
  ObjectRegistrationFunctor functor(reg_config, params);

  auto result = functor(*dsg, match);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(NodeSymbol('a', 1), result.from_node);
  EXPECT_EQ(NodeSymbol('a', 0), result.to_node);

  double err = gtsam::Pose3::Logmap(to_T_from.between(result.to_T_from)).norm();
  EXPECT_NEAR(0.0, err, 1.0e-3);
}

TEST_F(DsgRegistrationTests, DISABLED_TestFullPlaceRegistration) {
  LayerSearchResults match;
  for (int i = 0; i < src_points.cols(); ++i) {
    match.query_nodes.insert(NodeSymbol('p', i + src_points.cols()));
    match.match_nodes.insert(NodeSymbol('p', i));
  }

  match.query_root = NodeSymbol('p', src_points.cols());
  match.match_root = NodeSymbol('p', 0);

  teaser::RobustRegistrationSolver::Params params;
  PlaceRegistrationFunctor functor(reg_config, params);

  auto result = functor(*dsg, match);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(NodeSymbol('a', 1), result.from_node);
  EXPECT_EQ(NodeSymbol('a', 0), result.to_node);

  double err = gtsam::Pose3::Logmap(to_T_from.between(result.to_T_from)).norm();
  EXPECT_NEAR(0.0, err, 1.0e-3);
}

TEST_F(LayerRegistrationTests, TestRepeatedRegistration) {
  teaser::RobustRegistrationSolver::Params params;
  params.estimate_scaling = false;
  teaser::RobustRegistrationSolver solver(params);

  LayerRegistrationProblem problem;
  problem.src_nodes = node_ids;
  problem.dest_nodes = node_ids;
  problem.dest_layer = dest_layer.get();

  auto solution =
      registerDsgLayer(reg_config,
                       solver,
                       problem,
                       *src_layer,
                       [](const SceneGraphNode& src, const SceneGraphNode& dest) {
                         return src.id == dest.id;
                       });

  ASSERT_TRUE(solution.valid);

  std::list<NodeId> partial_list;
  for (size_t i = 0; i < 10; ++i) {
    partial_list.push_back(i);
  }

  LayerRegistrationProblem problem2;
  problem2.src_nodes = partial_list;
  problem2.dest_nodes = partial_list;
  problem2.dest_layer = dest_layer.get();

  solution =
      registerDsgLayer(reg_config,
                       solver,
                       problem2,
                       *src_layer,
                       [](const SceneGraphNode& src, const SceneGraphNode& dest) {
                         return src.id == dest.id;
                       });
  ASSERT_TRUE(solution.valid);
}

}  // namespace lcd
}  // namespace kimera