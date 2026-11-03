#pragma once

#include <QByteArray>
#include <QList>
#include <QVector>

// Vectors are stored as little-endian IEEE-754 Float32 BLOBs, dimensions * 4
// bytes, already scaled to unit length so similarity is a dot product.
namespace VectorCodec
{
[[nodiscard]] QByteArray encode(const QVector<float> &vector);
// Empty when the blob is not `dimensions` floats long.
[[nodiscard]] QVector<float> decode(const QByteArray &blob, int dimensions);
// Scales to unit length. A zero vector stays zero.
[[nodiscard]] QVector<float> normalized(QVector<float> vector);
// Dot product of a unit query with a stored blob, without copying it.
// Returns 0 when the sizes differ.
[[nodiscard]] float dot(const QVector<float> &unitQuery, const QByteArray &blob);
}
