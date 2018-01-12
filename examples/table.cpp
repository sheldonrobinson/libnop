// Copyright 2017 The Native Object Protocols Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#include <nop/base/table.h>
#include <nop/serializer.h>
#include <nop/status.h>
#include <nop/utility/die.h>
#include <nop/utility/stream_reader.h>
#include <nop/utility/stream_writer.h>

#include "stream_utilities.h"
#include "string_to_hex.h"

using nop::ActiveEntry;
using nop::DeletedEntry;
using nop::Deserializer;
using nop::Entry;
using nop::ErrorStatus;
using nop::Serializer;
using nop::Status;
using nop::StreamReader;
using nop::StreamWriter;
using nop::StringToHex;

//
// This example is a simple demonstration of tables. Tables are similar to
// regular serializable structures with some extra features to support
// bi-directional binary compatibility. The main advantage of tables is that
// data generated by one version of a table can be handled by other versions of
// the table, both older and newer. This property is important in many
// situations where requirements are expected to evolve over time.
//
// A table is a class or structure with members of type nop::Entry<T, Id>. These
// members are called the table's entries. The macro NOP_TABLE() is used to
// describe the table and its entries to the serialization engine. Each entry
// has a type, which may be any serializable type, and a numeric id that is
// unique among the entries of the same table. Entry ids should not change or be
// reused as a table evolves over time or else compatability between different
// versions of data will be broken. Entries may be either public, protected, or
// private as needed by the particular use case.
//
// nop::Entry<T, Id> may either be empty or store a value of type T. When an
// entry is empty it is not written during serialization, saving space in the
// output. Application code can test whether an entry is empty and take
// appropriate default action in case it is. This property supports both
// optionality and version compatability in a consistent manner.
//
// In this example three different versions of the same table are defined. In
// the real world these would all have the same C++ type name and the changes
// would be separated in time. Since all three versions need to coexist in this
// example, the three versions are defined in separate namespaces to keep the
// compiler happy.
//

namespace version1 {

// The first version of the table with a single member.
struct TableA {
  Entry<std::string, 0> a;

  NOP_TABLE("TableA", TableA, a);
};

}  // namespace version1

namespace version2 {

// The second version of the table that adds a member.
struct TableA {
  Entry<std::string, 0> a;
  Entry<std::vector<int>, 1> b;

  NOP_TABLE("TableA", TableA, a, b);
};

}  // namespace version2

namespace version3 {

// The third version of the table that deletes a member.
struct TableA {
  Entry<std::string, 0> a;
  Entry<std::vector<int>, 1, DeletedEntry> b;

  NOP_TABLE("TableA", TableA, a, b);
};

}  // namespace version3

namespace {

template <typename T, std::uint64_t Id>
std::ostream& operator<<(std::ostream& stream,
                         const Entry<T, Id, ActiveEntry>& entry) {
  if (entry)
    stream << entry.get();
  else
    stream << "<empty>";

  return stream;
}

template <typename T, std::uint64_t Id>
std::ostream& operator<<(std::ostream& stream,
                         const Entry<T, Id, DeletedEntry>& entry) {
  stream << "<deleted>";
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const version1::TableA& table) {
  stream << "version1::TableA{" << table.a << "}";
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const version2::TableA& table) {
  stream << "version2::TableA{" << table.a << ", " << table.b << "}";
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const version3::TableA& table) {
  stream << "version3::TableA{" << table.a << ", " << table.b << "}";
  return stream;
}

// Prints an error message to std::cerr when the Status<T> || Die() expression
// evaluates to false.
auto Die(const char* error_message = "Error") {
  return nop::Die(std::cerr, error_message);
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  Serializer<StreamWriter<std::stringstream>> serializer;

  // Initialize the first version of TableA and serialize it. The serialized
  // form is stored as a std::string in t1_data for reading back later.
  version1::TableA t1{"Version 1"};
  serializer.Write(t1) || Die("Failed to write t1");
  const std::string t1_data = serializer.writer().stream().str();
  serializer.writer().stream().str("");

  std::cout << "Wrote t1: " << t1 << std::endl;
  std::cout << "Serialized data: " << StringToHex(t1_data) << std::endl;
  std::cout << t1_data.size() << " bytes" << std::endl << std::endl;

  // Initialize the second version of TableA and serialize it. The serialized
  // form is stored as a std::string in t2_data for reading back later.
  version2::TableA t2{"Version 2", {1, 2, 3, 4}};
  serializer.Write(t2) || Die("Failed to write t2");
  const std::string t2_data = serializer.writer().stream().str();
  serializer.writer().stream().str("");

  std::cout << "Wrote t2: " << t2 << std::endl;
  std::cout << "Serialized data: " << StringToHex(t2_data) << std::endl;
  std::cout << t2_data.size() << " bytes" << std::endl << std::endl;

  // Initialize the third version of TableA and serialize it. The serialized
  // form is stored as a std::string in t3_data for reading back later.
  version3::TableA t3{"Version 3", {}};
  serializer.Write(t3) || Die("Failed to write t3");
  const std::string t3_data = serializer.writer().stream().str();
  serializer.writer().stream().str("");

  std::cout << "Wrote t3: " << t3 << std::endl;
  std::cout << "Serialized data: " << StringToHex(t3_data) << std::endl;
  std::cout << t3_data.size() << " bytes" << std::endl << std::endl;

  Deserializer<StreamReader<std::stringstream>> deserializer{};

  // Use the first version of TableA to read back the serialized data from each
  // version of the table.
  {
    deserializer.reader().stream().str(t1_data);

    version1::TableA table;
    deserializer.Read(&table) || Die("Failed to read t1_data");
    std::cout << "Read t1_data: " << table << std::endl;
  }

  {
    deserializer.reader().stream().str(t2_data);

    version1::TableA table;
    deserializer.Read(&table) || Die("Failed to read t2_data");
    std::cout << "Read t2_data: " << table << std::endl;
  }

  {
    deserializer.reader().stream().str(t3_data);

    version1::TableA table;
    deserializer.Read(&table) || Die("Failed to read t3_data");
    std::cout << "Read t3_data: " << table << std::endl;
  }

  // Use the second version of TableA to read back the serialized data from each
  // version of the table.
  {
    deserializer.reader().stream().str(t1_data);

    version2::TableA table;
    deserializer.Read(&table) || Die("Failed to read t1_data");
    std::cout << "Read t1_data: " << table << std::endl;
  }

  {
    deserializer.reader().stream().str(t2_data);

    version2::TableA table;
    deserializer.Read(&table) || Die("Failed to read t2_data");
    std::cout << "Read t2_data: " << table << std::endl;
  }

  {
    deserializer.reader().stream().str(t3_data);

    version2::TableA table;
    deserializer.Read(&table) || Die("Failed to read t3_data");
    std::cout << "Read t3_data: " << table << std::endl;
  }

  // Use the third version of TableA to read back the serialized data from each
  // version of the table.
  {
    deserializer.reader().stream().str(t1_data);

    version3::TableA table;
    deserializer.Read(&table) || Die("Failed to read t1_data");
    std::cout << "Read t1_data: " << table << std::endl;
  }

  {
    deserializer.reader().stream().str(t2_data);

    version3::TableA table;
    deserializer.Read(&table) || Die("Failed to read t2_data");
    std::cout << "Read t2_data: " << table << std::endl;
  }

  {
    deserializer.reader().stream().str(t3_data);

    version3::TableA table;
    deserializer.Read(&table) || Die("Failed to read t3_data");
    std::cout << "Read t3_data: " << table << std::endl;
  }

  return 0;
}
