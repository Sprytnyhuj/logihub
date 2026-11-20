#pragma once
// Głośność / wyciszenie przez PipeWire (pw-dump + wpctl).
#include <QList>
#include <QString>

struct AudioNode {
    int id = -1;
    QString description;
    bool isSink = true; // false = źródło (mikrofon)
};

namespace audio {
QList<AudioNode> findNodes(const QString &keyword);
// Zwraca głośność 0..150 lub -1; muted ustawiane gdy podano wskaźnik.
int volume(int id, bool *muted = nullptr);
void setVolume(int id, int percent);
void setMuted(int id, bool muted);
}
