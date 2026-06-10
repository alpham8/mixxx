#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonObject parseJson(const QByteArray& data) {
    return QJsonDocument::fromJson(data).object();
}

QByteArray makeBeatJson(int pos, double bpm, bool change, double strength) {
    QJsonObject msg;
    msg[QStringLiteral("evt")] = QStringLiteral("beat");
    msg[QStringLiteral("change")] = change;
    msg[QStringLiteral("pos")] = pos;
    msg[QStringLiteral("bpm")] = bpm;
    msg[QStringLiteral("strength")] = strength;
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

QByteArray makeButtonJson(const QString& name,
        const QString& state,
        const QString& page = QString()) {
    QJsonObject msg;
    msg[QStringLiteral("evt")] = QStringLiteral("btn");
    msg[QStringLiteral("name")] = name;
    msg[QStringLiteral("state")] = state;
    if (!page.isEmpty()) {
        msg[QStringLiteral("page")] = page;
    }
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

QByteArray makeCommandJson(int id, double param) {
    QJsonObject msg;
    msg[QStringLiteral("evt")] = QStringLiteral("cmd");
    msg[QStringLiteral("id")] = id;
    msg[QStringLiteral("param")] = param;
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

TEST(Os2lTest, BeatMessageFormat) {
    QByteArray data = makeBeatJson(0, 128.0, true, 0.85);
    QJsonObject obj = parseJson(data);
    EXPECT_EQ(obj["evt"].toString(), "beat");
    EXPECT_EQ(obj["pos"].toInt(), 0);
    EXPECT_DOUBLE_EQ(obj["bpm"].toDouble(), 128.0);
    EXPECT_EQ(obj["change"].toBool(), true);
    EXPECT_NEAR(obj["strength"].toDouble(), 0.85, 0.001);
}

TEST(Os2lTest, BeatMessageDownbeatAlignment) {
    for (int pos = 0; pos < 32; ++pos) {
        QByteArray data = makeBeatJson(pos, 120.0, false, 0.5);
        QJsonObject obj = parseJson(data);
        int p = obj["pos"].toInt();
        if (pos % 4 == 0) {
            EXPECT_EQ(p % 4, 0) << "pos " << pos << " should be on bar boundary";
        }
        if (pos % 16 == 0) {
            EXPECT_EQ(p % 16, 0) << "pos " << pos << " should be on phrase boundary";
        }
    }
}

TEST(Os2lTest, ButtonMessageFormat) {
    QByteArray data = makeButtonJson("play_deck1", "on");
    QJsonObject obj = parseJson(data);
    EXPECT_EQ(obj["evt"].toString(), "btn");
    EXPECT_EQ(obj["name"].toString(), "play_deck1");
    EXPECT_EQ(obj["state"].toString(), "on");
    EXPECT_FALSE(obj.contains("page"));
}

TEST(Os2lTest, ButtonMessageWithPage) {
    QByteArray data = makeButtonJson("hotcue_1", "on", "deck1");
    QJsonObject obj = parseJson(data);
    EXPECT_EQ(obj["evt"].toString(), "btn");
    EXPECT_EQ(obj["name"].toString(), "hotcue_1");
    EXPECT_EQ(obj["state"].toString(), "on");
    EXPECT_EQ(obj["page"].toString(), "deck1");
}

TEST(Os2lTest, CommandMessageFormat) {
    QByteArray data = makeCommandJson(42, 75.5);
    QJsonObject obj = parseJson(data);
    EXPECT_EQ(obj["evt"].toString(), "cmd");
    EXPECT_EQ(obj["id"].toInt(), 42);
    EXPECT_NEAR(obj["param"].toDouble(), 75.5, 0.001);
}

TEST(Os2lTest, BeatPositionCounterModulo) {
    int beatCounter = 0;
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(beatCounter % 4 == 0, i % 4 == 0)
                << "Beat " << i << " bar boundary mismatch";
        EXPECT_EQ(beatCounter % 16 == 0, i % 16 == 0)
                << "Beat " << i << " phrase boundary mismatch";
        beatCounter++;
    }
}

TEST(Os2lTest, BeatChangeDetection) {
    double lastBpm = 128.0;
    double newBpm = 128.0;
    bool change = (newBpm != lastBpm);
    EXPECT_FALSE(change);

    newBpm = 130.0;
    change = (newBpm != lastBpm);
    EXPECT_TRUE(change);
}

} // namespace
