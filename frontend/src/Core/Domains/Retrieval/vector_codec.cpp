#include "vector_codec.h"

#include <QtEndian>
#include <cmath>
#include <cstring>

namespace VectorCodec
{
QByteArray encode(const QVector<float> &vector)
{
    QByteArray blob(vector.size() * 4, Qt::Uninitialized);
    auto *out = reinterpret_cast<uchar *>(blob.data());

    for (qsizetype index = 0; index < vector.size(); ++index) {
        quint32 bits = 0;
        std::memcpy(&bits, &vector[index], 4);
        qToLittleEndian(bits, out + index * 4);
    }

    return blob;
}

QVector<float> decode(const QByteArray &blob, int dimensions)
{
    if (dimensions <= 0 || blob.size() != static_cast<qsizetype>(dimensions) * 4) {
        return {};
    }

    QVector<float> vector(dimensions);
    const auto *in = reinterpret_cast<const uchar *>(blob.constData());

    for (int index = 0; index < dimensions; ++index) {
        const quint32 bits = qFromLittleEndian<quint32>(in + index * 4);
        std::memcpy(&vector[index], &bits, 4);
    }

    return vector;
}

QVector<float> normalized(QVector<float> vector)
{
    double sum = 0;

    for (const float value : vector) {
        sum += static_cast<double>(value) * value;
    }

    if (sum <= 0 || !std::isfinite(sum)) {
        return vector;
    }

    const float scale = static_cast<float>(1.0 / std::sqrt(sum));

    for (float &value : vector) {
        value *= scale;
    }

    return vector;
}

float dot(const QVector<float> &unitQuery, const QByteArray &blob)
{
    if (blob.size() != unitQuery.size() * 4) {
        return 0;
    }

    const auto *in = reinterpret_cast<const uchar *>(blob.constData());
    double sum = 0;

    for (qsizetype index = 0; index < unitQuery.size(); ++index) {
        float value = 0;
        if constexpr (Q_BYTE_ORDER == Q_LITTLE_ENDIAN) {
            std::memcpy(&value, in + index * 4, 4);
        } else {
            const quint32 bits = qFromLittleEndian<quint32>(in + index * 4);
            std::memcpy(&value, &bits, 4);
        }
        sum += static_cast<double>(unitQuery[index]) * value;
    }

    return static_cast<float>(sum);
}
}
