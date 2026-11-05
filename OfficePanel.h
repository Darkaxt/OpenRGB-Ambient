#pragma once
#include "OfficeLayout.h"
#include <functional>
#include <QString>
class QWidget;

// A typed in-process message crosses the GUI/lighting thread boundary. It uses
// the same validated command handler as diagnostics, without serializing a pipe.
using OfficeReply = std::function<void(const office::Json&)>;
using OfficeDispatch = std::function<void(office::Json, OfficeReply)>;
QWidget* createOfficePanel(const QString& state, OfficeDispatch dispatch);
