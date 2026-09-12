using System.Runtime.InteropServices;

namespace TomCat;

[StructLayout(LayoutKind.Sequential)]
public struct Vector2 : IEquatable<Vector2>
{
    public float X;
    public float Y;

    public Vector2(float x, float y) => (X, Y) = (x, y);

    public static Vector2 Zero => default;
    public static Vector2 One => new(1.0f, 1.0f);
    public static Vector2 operator +(Vector2 left, Vector2 right) => new(left.X + right.X, left.Y + right.Y);
    public static Vector2 operator -(Vector2 left, Vector2 right) => new(left.X - right.X, left.Y - right.Y);
    public static Vector2 operator *(Vector2 value, float scalar) => new(value.X * scalar, value.Y * scalar);
    public readonly bool Equals(Vector2 other) => X.Equals(other.X) && Y.Equals(other.Y);
    public override readonly bool Equals(object? obj) => obj is Vector2 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y);
    public override readonly string ToString() => $"({X}, {Y})";
}
[StructLayout(LayoutKind.Sequential)]
public struct Vector3 : IEquatable<Vector3>
{
    public float X;
    public float Y;
    public float Z;

    public Vector3(float x, float y, float z) => (X, Y, Z) = (x, y, z);

    public static Vector3 Zero => default;
    public static Vector3 One => new(1.0f, 1.0f, 1.0f);
    public static Vector3 operator +(Vector3 left, Vector3 right) => new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
    public static Vector3 operator -(Vector3 left, Vector3 right) => new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
    public static Vector3 operator *(Vector3 value, float scalar) => new(value.X * scalar, value.Y * scalar, value.Z * scalar);
    public readonly bool Equals(Vector3 other) => X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z);
    public override readonly bool Equals(object? obj) => obj is Vector3 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override readonly string ToString() => $"({X}, {Y}, {Z})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Vector4 : IEquatable<Vector4>
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public Vector4(float x, float y, float z, float w) => (X, Y, Z, W) = (x, y, z, w);

    public static Vector4 Zero => default;
    public static Vector4 One => new(1.0f, 1.0f, 1.0f, 1.0f);
    public readonly bool Equals(Vector4 other) => X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z) && W.Equals(other.W);
    public override readonly bool Equals(object? obj) => obj is Vector4 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override readonly string ToString() => $"({X}, {Y}, {Z}, {W})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Color : IEquatable<Color>
{
    public float R;
    public float G;
    public float B;
    public float A;

    public Color(float r, float g, float b, float a = 1.0f) => (R, G, B, A) = (r, g, b, a);

    public static Color White => new(1.0f, 1.0f, 1.0f, 1.0f);
    public static Color Black => new(0.0f, 0.0f, 0.0f, 1.0f);
    public readonly bool Equals(Color other) => R.Equals(other.R) && G.Equals(other.G) && B.Equals(other.B) && A.Equals(other.A);
    public override readonly bool Equals(object? obj) => obj is Color other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(R, G, B, A);
    public override readonly string ToString() => $"({R}, {G}, {B}, {A})";
}

[StructLayout(LayoutKind.Sequential)]
public struct Matrix4 : IEquatable<Matrix4>
{
    public float M11, M12, M13, M14;
    public float M21, M22, M23, M24;
    public float M31, M32, M33, M34;
    public float M41, M42, M43, M44;

    public static Matrix4 Identity => new() { M11 = 1, M22 = 1, M33 = 1, M44 = 1 };
    public readonly bool Equals(Matrix4 other) =>
        M11 == other.M11 && M12 == other.M12 && M13 == other.M13 && M14 == other.M14 &&
        M21 == other.M21 && M22 == other.M22 && M23 == other.M23 && M24 == other.M24 &&
        M31 == other.M31 && M32 == other.M32 && M33 == other.M33 && M34 == other.M34 &&
        M41 == other.M41 && M42 == other.M42 && M43 == other.M43 && M44 == other.M44;
    public override readonly bool Equals(object? obj) => obj is Matrix4 other && Equals(other);
    public override readonly int GetHashCode() => HashCode.Combine(
        HashCode.Combine(M11, M12, M13, M14), HashCode.Combine(M21, M22, M23, M24),
        HashCode.Combine(M31, M32, M33, M34), HashCode.Combine(M41, M42, M43, M44));
}
