// Test double for Database/DatabaseEnv.h, for the perfmon collection suite.
//
// PerformanceMonitor.cpp includes it and then never touches the database: the
// monitor keeps everything in memory. Deliberately empty -- a stub with a
// connection in it would be inventing a dependency the file under test does not
// have.

#pragma once
