#include <cstdio>

#include "io/carp.h"
#include "util/Params.h"
#include "PSMConvertApplication.h"
#include "io/PepXMLWriter.h"
#include "io/PSMReader.h"
#include "io/PSMWriter.h"
#include "model/MatchCollection.h"
#include "model/ProteinMatchCollection.h"
#include "io/PMCPepXMLWriter.h"

#include "io/MatchFileReader.h"
#include "io/MzIdentMLReader.h"
#include "io/PepXMLReader.h"
#include "io/SQTReader.h"

#include "io/HTMLWriter.h"
#include "io/MzIdentMLWriter.h"
#include "io/PinWriter.h"
#include "io/PMCDelimitedFileWriter.h"
#include "io/PMCPepXMLWriter.h"
#include "io/PMCSQTWriter.h"

PSMConvertApplication::PSMConvertApplication() {
}

PSMConvertApplication::~PSMConvertApplication() {
}

int PSMConvertApplication::main(int argc, char** argv) {

  bool overwrite = Params::GetBool("overwrite");

  carp(CARP_INFO, "Running psm-convert...");

  string database_file = Params::GetString("protein-database");

  Database* data;

  if (database_file.empty()) {
    data = new Database();
    carp(CARP_INFO, "Database not provided, will use empty database");
  } else {
    data = new Database(database_file.c_str(), false);
    carp(CARP_INFO, "Created Database using Fasta File");
  }

  PSMReader* reader;
  string input_format = Params::GetString("input-format");
  string input_file = Params::GetString("input PSM file");

  bool isTabDelimited = false;

  if (input_format != "auto") {
    if (input_format == "tsv") {
      reader = new MatchFileReader(input_file.c_str(), data);
      isTabDelimited = true;
    } else if (input_format == "html") {
      carp(CARP_FATAL, "HTML format has not been implemented yet");
      reader = new MatchFileReader(input_file.c_str(), data);
    } else if (input_format == "sqt") {
      reader = new SQTReader(input_file.c_str(), data);
    } else if (input_format == "pin") {
      carp(CARP_FATAL, "Pin format has not been implemented yet");
      reader = new MatchFileReader(input_file.c_str(), data);
    } else if (input_format == "pepxml") {
      reader = new PepXMLReader(input_file.c_str(), data);
    } else if (input_format == "mzidentml") {
      reader = new MzIdentMLReader(input_file.c_str(), data);
    } else if (input_format == "barista-xml") {
      carp(CARP_FATAL, "Barista-XML format has not been implemented yet");
      reader = new MatchFileReader(input_file.c_str(), data);
    } else {
      carp(CARP_FATAL, "Invalid Input Format, valid formats are: tsv, html, "
        "sqt, pin, pepxml, mzidentml, barista-xml");
    }
  } else {
    if (StringUtils::IEndsWith(input_file, ".txt")) {
      reader = new MatchFileReader(input_file.c_str(), data);
      isTabDelimited = true;
    } else if (StringUtils::IEndsWith(input_file, ".html")) {
      carp(CARP_FATAL, "HTML format has not been implemented yet");
      reader = new MatchFileReader(input_file.c_str(), data);
    } else if (StringUtils::IEndsWith(input_file, ".sqt")) {
      reader = new SQTReader(input_file.c_str(), data);
    } else if (StringUtils::IEndsWith(input_file, ".pin")) {
      carp(CARP_FATAL, "Pin format has not been implemented yet");
      reader = new MatchFileReader(input_file.c_str(), data);
    } else if (StringUtils::IEndsWith(input_file, ".xml")) {
      reader = new PepXMLReader(input_file.c_str(), data);
    } else if (StringUtils::IEndsWith(input_file, ".mzid")) {
      reader = new MzIdentMLReader(input_file.c_str(), data);
    } else if (StringUtils::IEndsWith(input_file, ".barista.xml")) {
      carp(CARP_FATAL, "Barista-XML format has not been implemented yet");
      reader = new MatchFileReader(input_file.c_str(), data);
    } else {
      carp(CARP_FATAL, "Could not determine input format, "
        "Please name your files ending with .txt, .html, .sqt, .pin, "
        ".xml, .mzid, .barista.xml or use the --input-format option to "
        "specify file type");
    }
  }

  MatchCollection* collection = reader->parse();

  if (!isTabDelimited) {
    collection->setHasDistinctMatches(Params::GetBool("distinct-matches"));
  } else if (collection->getHasDistinctMatches() != Params::GetBool("distinct-matches")) {
    const char* matchType = collection->getHasDistinctMatches() ?
      "distinct" : "not distinct";
    carp(CARP_WARNING, "Parser has detected that matches are %s, but parameter "
                       "distinct-matches is set to %s. We will assume that matches are %s",
         matchType, Params::GetBool("distinct-matches") ? "distinct" : "not distinct",
         matchType);
  }

  carp(CARP_INFO, "Reader has been succesfully parsed");

  string output_format = Params::GetString("output format");

  // What will be used when PSMWriter is finished.

  PSMWriter* writer;
  stringstream output_file_name_builder;
  output_file_name_builder << "psm-convert.";

  if (output_format == ("tsv")) {
    output_file_name_builder << "txt";
    writer = new PMCDelimitedFileWriter();
  } else if (output_format == ("html")) {
    output_file_name_builder << "html";
    writer = new HTMLWriter();
  } else if (output_format == ("sqt")) {
    output_file_name_builder << "sqt";
    writer = new PMCSQTWriter();
  } else if (output_format == ("pin")) {
    output_file_name_builder << "pin";
    writer = new PinWriter();
  } else if (output_format == ("pepxml")) {
    output_file_name_builder << "pep.xml";
    writer = new PMCPepXMLWriter();
  } else if (output_format == ("mzidentml")) {
    output_file_name_builder << "mzid";
    writer = new MzIdentMLWriter();
  } else if (output_format == ("barista-xml")) {
    carp(CARP_FATAL, "Barista-XML format has not been implemented yet");
    output_file_name_builder << "barista.xml";
    writer = new PMCDelimitedFileWriter();
  } else {
      carp(CARP_FATAL, "Invalid Input Format, valid formats are: tsv, html, "
        "sqt, pin, pepxml, mzidentml, barista-xml");
    writer = new PMCDelimitedFileWriter();
  }

  string output_file_name = make_file_path(output_file_name_builder.str());

  writer->openFile(this, output_file_name, PSMWriter::PSMS);
  writer->write(collection, database_file);
  writer->closeFile();

  // Clean Up
  delete collection;
  delete reader;
  delete writer;

  return 0;
}

string PSMConvertApplication::getName() const {
  return "psm-convert";
}

string PSMConvertApplication::getDescription() const {
  return
  "Reads in a file containing peptide-spectrum matches "
  "(PSMs) in one of the variety of supported formats and "
  "outputs the same PSMs in a different format";
}

vector<string> PSMConvertApplication::getArgs() const {
  string arr[] = {
    "input PSM file",
    "output format"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<string> PSMConvertApplication::getOptions() const {
  string arr[] = {
    "input-format",
    "protein-database",
    "output-dir",
    "overwrite",
    "parameter-file",
    "verbosity",
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

bool PSMConvertApplication::needsOutputDirectory() const {
  return true;
}

COMMAND_T PSMConvertApplication::getCommand() const {
  return PSM_CONVERT_COMMAND;
}

