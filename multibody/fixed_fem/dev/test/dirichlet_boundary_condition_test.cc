#include "drake/multibody/fixed_fem/dev/dirichlet_boundary_condition.h"

#include <gtest/gtest.h>

#include "drake/common/test_utilities/eigen_matrix_compare.h"
#include "drake/common/test_utilities/expect_throws_message.h"
#include "drake/multibody/fixed_fem/dev/fem_state.h"
#include "drake/multibody/fixed_fem/dev/test/dummy_element.h"

namespace drake {
namespace multibody {
namespace fem {
namespace test {
namespace {

/* An arbitrary number of degree of freedom made up for testing purpose. */
static constexpr int kNumDofs = 4;
using DenseMatrix = Eigen::Matrix<double, kNumDofs, kNumDofs>;
using Eigen::VectorXd;
using SparseMatrix = Eigen::SparseMatrix<double>;
using Element = DummyElement<1>;
constexpr int kOdeOrder = Element::Traits::kOdeOrder;
using State = FemState<Element>;
using internal::PetscSymmetricBlockSparseMatrix;
using std::make_unique;
using std::unique_ptr;
using std::vector;

class DirichletBoundaryConditionTest : public ::testing::Test {
 protected:
  void SetUp() {
    bc_.AddBoundaryCondition(DofIndex(0), Vector2<double>(3, 2));
    bc_.AddBoundaryCondition(DofIndex(2), Vector2<double>(1, 0));
  }

  /* Makes an arbitrary residual vector with appropriate size. */
  static VectorXd MakeResidual() {
    return Vector<double, kNumDofs>(1.1, 2.2, 3.3, 4.4);
  }

  /* Makes an arbitrary SPD tangent matrix with appropriate size. */
  static SparseMatrix MakeEigenTangentMatrix() {
    DenseMatrix A;
    // clang-format off
    A << 3,   1,     0.5, -1,
         1,   2,     0.25, 0.125,
         0.5, 0.25,  3,    0.25,
        -1,   0.125, 0.25, 4;
    // clang-format on
    SparseMatrix A_eigen_sparse = A.sparseView();
    return A_eigen_sparse;
  }

  /* Returns the same matrix as in MakeEigenTangentMatrix(), but in PETSc
   format. */
  static unique_ptr<PetscSymmetricBlockSparseMatrix> MakePetscTangentMatrix() {
    const vector<int> num_upper_triangular_blocks_per_row = {1};
    auto A = make_unique<PetscSymmetricBlockSparseMatrix>(
        kNumDofs, kNumDofs, num_upper_triangular_blocks_per_row);
    const Vector1<int> index(0);
    A->AddToBlock(index, DenseMatrix(MakeEigenTangentMatrix()));
    A->AssembleIfNecessary();
    return A;
  }

  /* Makes an arbitrary compatible FemState with appropriate size. */
  static State MakeState() {
    State state{Vector<double, kNumDofs>(0.1, 0.2, 0.3, 0.4),
                Vector<double, kNumDofs>(0.5, 0.6, 0.7, 0.8)};
    return state;
  }

  /* The DirichletBoundaryCondition under test. */
  DirichletBoundaryCondition<double> bc_{kOdeOrder};
};

/* Tests that the DirichletBoundaryCondition under test successfully modifies
 a given state. */
TEST_F(DirichletBoundaryConditionTest, ApplyBoundaryConditionToState) {
  State s = MakeState();
  s.ApplyBoundaryCondition(bc_);
  EXPECT_TRUE(CompareMatrices(s.q(), Vector<double, kNumDofs>{3, 0.2, 1, 0.4}));
  EXPECT_TRUE(
      CompareMatrices(s.qdot(), Vector<double, kNumDofs>{2, 0.6, 0, 0.8}));
}

/* Tests that the DirichletBoundaryCondition under test successfully modifies
 a given residual. */
TEST_F(DirichletBoundaryConditionTest, ApplyBoundaryConditionToResidual) {
  VectorXd b = MakeResidual();
  bc_.ApplyBoundaryConditionToResidual(&b);
  const Vector<double, kNumDofs> b_expected(0, 2.2, 0, 4.4);
  EXPECT_TRUE(CompareMatrices(b, b_expected));
}

/* Tests that the DirichletBoundaryCondition under test successfully modifies a
 given tangent matrix. */
TEST_F(DirichletBoundaryConditionTest,
       ApplyBoundaryConditionResidualAndTangentMatrix) {
  /* Test for the Eigen::SparseMatrix overload. */
  SparseMatrix A_eigen_sparse = MakeEigenTangentMatrix();
  const int nnz = A_eigen_sparse.nonZeros();
  bc_.ApplyBoundaryConditionToTangentMatrix(&A_eigen_sparse);
  /* The number of nonzeros should not change. */
  EXPECT_EQ(A_eigen_sparse.nonZeros(), nnz);
  const DenseMatrix A(A_eigen_sparse);
  DenseMatrix A_expected;
  // clang-format off
  A_expected << 1, 0,     0, 0,
                0, 2,     0, 0.125,
                0, 0,     1, 0,
                0, 0.125, 0, 4;
  // clang-format on
  EXPECT_TRUE(CompareMatrices(A, A_expected));

  /* Test for the PetscSymmetricBlockSparseMatrix overload. */
  auto A_petsc = MakePetscTangentMatrix();
  bc_.ApplyBoundaryConditionToTangentMatrix(A_petsc.get());
  EXPECT_TRUE(CompareMatrices(A_petsc->MakeDenseMatrix(), A_expected));
}

/* Tests out-of-bound boundary conditions throw an exception. */
TEST_F(DirichletBoundaryConditionTest, OutOfBound) {
  /* Put a dof that is out-of-bound under boundary condition. */
  bc_.AddBoundaryCondition(DofIndex(4), Vector<double, kOdeOrder + 1>(3, 4));
  State state = MakeState();
  DRAKE_EXPECT_THROWS_MESSAGE(
      state.ApplyBoundaryCondition(bc_),
      "An index of the dirichlet boundary condition is out of the range.");
  VectorXd b = MakeResidual();
  DRAKE_EXPECT_THROWS_MESSAGE(
      bc_.ApplyBoundaryConditionToResidual(&b),
      "An index of the dirichlet boundary condition is out of the range.");
  SparseMatrix A_eigen_sparse = MakeEigenTangentMatrix();
  DRAKE_EXPECT_THROWS_MESSAGE(
      bc_.ApplyBoundaryConditionToTangentMatrix(&A_eigen_sparse),
      "An index of the dirichlet boundary condition is out of the range.");
  auto A_petsc = MakePetscTangentMatrix();
  DRAKE_EXPECT_THROWS_MESSAGE(
      bc_.ApplyBoundaryConditionToTangentMatrix(A_petsc.get()),
      "An index of the dirichlet boundary condition is out of the range.");
}

}  // namespace
}  // namespace test
}  // namespace fem
}  // namespace multibody
}  // namespace drake
