#pragma once
#include "Vector.h"
#include <cstring>

#pragma warning(push)
#pragma warning(disable: 4201) // nonstandard extension used: nameless struct/union

namespace Fujin {

	struct Matrix2x2 {
		float m[2][2];

		Matrix2x2() {
			m[0][0] = 1.0f; m[0][1] = 0.0f;
			m[1][0] = 0.0f; m[1][1] = 1.0f;
		}

		static const Matrix2x2 Identity;
	};

	struct Matrix3x3 {
		float m[3][3];

		Matrix3x3() {
			std::memset(m, 0, sizeof(m));
			m[0][0] = 1.0f; m[1][1] = 1.0f; m[2][2] = 1.0f;
		}

		static const Matrix3x3 Identity;
	};

	struct Matrix4x4 {
		union {
			float m[4][4];
			float v[16];
			struct {
				float m00, m01, m02, m03;
				float m10, m11, m12, m13;
				float m20, m21, m22, m23;
				float m30, m31, m32, m33;
			};
		};

		Matrix4x4() {
			SetIdentity();
		}

		void SetIdentity() {
			std::memset(v, 0, sizeof(v));
			m[0][0] = 1.0f;
			m[1][1] = 1.0f;
			m[2][2] = 1.0f;
			m[3][3] = 1.0f;
		}

		static const Matrix4x4 Identity;

		Matrix4x4 operator*(const Matrix4x4& other) const {
			Matrix4x4 result;
			for (int i = 0; i < 4; ++i) {
				for (int j = 0; j < 4; ++j) {
					result.m[i][j] = m[i][0] * other.m[0][j] +
						m[i][1] * other.m[1][j] +
						m[i][2] * other.m[2][j] +
						m[i][3] * other.m[3][j];
				}
			}
			return result;
		}

		Vector4 operator*(const Vector4& vec) const {
			return Vector4(
				m[0][0] * vec.x + m[0][1] * vec.y + m[0][2] * vec.z + m[0][3] * vec.w,
				m[1][0] * vec.x + m[1][1] * vec.y + m[1][2] * vec.z + m[1][3] * vec.w,
				m[2][0] * vec.x + m[2][1] * vec.y + m[2][2] * vec.z + m[2][3] * vec.w,
				m[3][0] * vec.x + m[3][1] * vec.y + m[3][2] * vec.z + m[3][3] * vec.w
			);
		}

		static Matrix4x4 Translation(const Vector3& pos) {
			Matrix4x4 result;
			result.m[0][3] = pos.x;
			result.m[1][3] = pos.y;
			result.m[2][3] = pos.z;
			return result;
		}

		Vector3 GetTranslation() const { return Vector3(m[0][3], m[1][3], m[2][3]); }

		static Matrix4x4 Scale(const Vector3& scale) {
			Matrix4x4 result;
			result.m[0][0] = scale.x;
			result.m[1][1] = scale.y;
			result.m[2][2] = scale.z;
			return result;
		}

		static Matrix4x4 RotationX(float angle) {
			Matrix4x4 result;
			float s = std::sin(angle);
			float c = std::cos(angle);
			result.m[1][1] = c; result.m[1][2] = -s;
			result.m[2][1] = s; result.m[2][2] = c;
			return result;
		}

		static Matrix4x4 RotationY(float angle) {
			Matrix4x4 result;
			float s = std::sin(angle);
			float c = std::cos(angle);
			result.m[0][0] = c;  result.m[0][2] = s;
			result.m[2][0] = -s; result.m[2][2] = c;
			return result;
		}

		static Matrix4x4 RotationZ(float angle) {
			Matrix4x4 result;
			float s = std::sin(angle);
			float c = std::cos(angle);
			result.m[0][0] = c; result.m[0][1] = -s;
			result.m[1][0] = s; result.m[1][1] = c;
			return result;
		}

		static Matrix4x4 Perspective(float fov, float aspect, float nearZ, float farZ) {
			Matrix4x4 result;
			std::memset(result.v, 0, sizeof(result.v));
			float tanHalfFov = std::tan(fov * 0.5f);
			result.m[0][0] = 1.0f / (aspect * tanHalfFov);
			result.m[1][1] = 1.0f / tanHalfFov;
			result.m[2][2] = farZ / (farZ - nearZ);
			result.m[2][3] = -(farZ * nearZ) / (farZ - nearZ);
			result.m[3][2] = 1.0f;
			return result;
		}

		static Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
			Vector3 zAxis = (target - eye).GetSafeNormal();
			Vector3 xAxis = Vector3::Cross(up, zAxis).GetSafeNormal();
			Vector3 yAxis = Vector3::Cross(zAxis, xAxis);

			Matrix4x4 result;
			result.m[0][0] = xAxis.x; result.m[0][1] = xAxis.y; result.m[0][2] = xAxis.z; result.m[0][3] = -Vector3::Dot(xAxis, eye);
			result.m[1][0] = yAxis.x; result.m[1][1] = yAxis.y; result.m[1][2] = yAxis.z; result.m[1][3] = -Vector3::Dot(yAxis, eye);
			result.m[2][0] = zAxis.x; result.m[2][1] = zAxis.y; result.m[2][2] = zAxis.z; result.m[2][3] = -Vector3::Dot(zAxis, eye);
			result.m[3][0] = 0.0f;    result.m[3][1] = 0.0f;    result.m[3][2] = 0.0f;    result.m[3][3] = 1.0f;
			return result;
		}

		void Transpose() {
			for (int i = 0; i < 4; ++i) {
				for (int j = i + 1; j < 4; ++j) {
					std::swap(m[i][j], m[j][i]);
				}
			}
		}

		Matrix4x4 GetTransposed() const {
			Matrix4x4 result = *this;
			result.Transpose();
			return result;
		}

		Matrix4x4 GetInverse() const {
			const float* a = v;
			float t[16];
			t[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
			t[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
			t[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
			t[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
			t[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
			t[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
			t[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
			t[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
			t[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
			t[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
			t[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
			t[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
			t[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
			t[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
			t[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
			t[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
			float det = a[0]*t[0] + a[1]*t[4] + a[2]*t[8] + a[3]*t[12];
			if (std::fabsf(det) < 1e-9f) return Matrix4x4::Identity;
			det = 1.0f / det;
			Matrix4x4 result;
			for (int i = 0; i < 16; ++i) result.v[i] = t[i] * det;
			return result;
		}
	};

} // namespace Fujin

#pragma warning(pop)
