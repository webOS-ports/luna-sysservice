// Copyright (c) 2010-2025 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "Logging.h"

#ifdef WEBOS_QT
#include <QtCore/QFileInfo>
#endif //WEBOS_QT

PmLogContext& sysServiceLogContext()
{
	static PmLogContext logContext = PmLogGetContextInline("LunaSysService");
	return logContext;
}

void setLogLevel(const std::string& loglstr)
{
	if (loglstr == "error")
		PmLogSetContextLevel(sysServiceLogContext(), kPmLogLevel_Error);
	else if (loglstr == "critical")
		PmLogSetContextLevel(sysServiceLogContext(), kPmLogLevel_Critical);
	else if (loglstr == "warning")
		 PmLogSetContextLevel(sysServiceLogContext(), kPmLogLevel_Warning);
	else if (loglstr == "debug")
		PmLogSetContextLevel(sysServiceLogContext(), kPmLogLevel_Debug);
	else // "info", and the default for anything else
		PmLogSetContextLevel(sysServiceLogContext(), kPmLogLevel_Info);
}

// A printf-style wrapper: the format attribute on the declaration has the
// compiler check every call's format string against its arguments.
void logInfo(const char* file, int line, const char* func, const char *fmt, ...) // NOLINT(cert-dcl50-cpp)
{
	std::string meta = std::string(file) + "#" + std::to_string(line);

	va_list args;
	va_start(args, fmt);
	va_list args_copy;
	va_copy(args_copy, args);
	int size = vsnprintf(nullptr, 0, fmt, args_copy); // Flawfinder: ignore - format checked at compile time
	va_end(args_copy);

	std::string data;
	if (size > 0)
	{
		data.resize(static_cast<size_t>(size) + 1); // room for vsnprintf's terminator
		if (vsnprintf(&data[0], data.size(), fmt, args) == size) // Flawfinder: ignore - as above
			data.resize(static_cast<size_t>(size));
		else
			data.clear();
	}
	va_end(args);

	PmLogInfo(sysServiceLogContext(), meta.c_str(), 1, PMLOGKS("FUNC", func), "%s", data.c_str());
}

#ifdef WEBOS_QT
void outputQtMessages(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	// file/function can be null in release builds
	const char *function = context.function ? context.function : "";
	//QString meta = QString("%1#%2").arg(QFileInfo(context.file).baseName()).arg(context.line);
	std::string meta = std::string(context.file ? context.file : "") + std::string("#") + std::to_string(context.line);
	const QByteArray text = msg.toUtf8();

	switch (type) {
		case QtDebugMsg:
#ifndef NO_LOGGING
			PmLogDebug(sysServiceLogContext(), meta.c_str(), 1, PMLOGKS("FUNC", function),
					   "%s", text.constData());
#endif
			break;
		case QtInfoMsg:
			PmLogInfo(sysServiceLogContext(), meta.c_str(), 1, PMLOGKS("FUNC", function),
					  "%s", text.constData());
			break;
		case QtWarningMsg:
			PmLogWarning(sysServiceLogContext(), meta.c_str(), 1, PMLOGKS("FUNC", function),
						 "%s", text.constData());
			break;
		case QtCriticalMsg:
			PmLogError(sysServiceLogContext(), meta.c_str(), 1, PMLOGKS("FUNC", function),
					   "%s", text.constData());
			break;
		case QtFatalMsg:
			PmLogCritical(sysServiceLogContext(), meta.c_str(), 1, PMLOGKS("FUNC", function),
						  "%s", text.constData());
			abort();
	}
}
#endif //WEBOS_QT
