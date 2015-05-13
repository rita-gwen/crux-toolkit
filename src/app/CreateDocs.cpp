#include <algorithm>
#include <cctype>
#include <functional>
#include <fstream>
#include <iostream>

#include "CreateDocs.h"
#include "CruxApplicationList.h"
#include "util/FileUtils.h"
#include "util/Params.h"

#include "qranker-barista/Barista.h"
#include "ComputeQValues.h"
#include "CruxBullseyeApplication.h"
#include "CruxHardklorApplication.h"
#include "ExtractColumns.h"
#include "ExtractRows.h"
#include "GeneratePeptides.h"
#include "GetMs2Spectrum.h"
#include "MakePinApplication.h"
#include "PercolatorApplication.h"
#include "Pipeline.h"
#include "PredictPeptideIons.h"
#include "PrintProcessedSpectra.h"
#include "qranker-barista/QRanker.h"
#include "ReadTideIndex.h"
#include "xlink/SearchForXLinks.h"
#include "SortColumn.h"
#include "SpectralCounts.h"
#include "StatColumn.h"
#include "TideIndexApplication.h"
#include "TideSearchApplication.h"
#include "CometApplication.h"

using namespace std;

CreateDocs::CreateDocs() {
}

CreateDocs::~CreateDocs() {
}

int CreateDocs::main(int argc, char** argv) {
  CruxApplicationList apps("crux");
  apps.add(new Barista());
  apps.add(new CometApplication());
  apps.add(new ComputeQValues());
  apps.add(new CreateDocs());
  apps.add(new CruxBullseyeApplication());
  apps.add(new CruxHardklorApplication());
  apps.add(new ExtractColumns());
  apps.add(new ExtractRows());
  apps.add(new GeneratePeptides());
  apps.add(new GetMs2Spectrum());
  apps.add(new MakePinApplication());
  apps.add(new PercolatorApplication());
  apps.add(new PipelineApplication());
  apps.add(new PredictPeptideIons());
  apps.add(new PrintProcessedSpectra());
  apps.add(new QRanker());
  apps.add(new ReadTideIndex());
  apps.add(new SearchForXLinks());
  apps.add(new SortColumn());
  apps.add(new SpectralCounts());
  apps.add(new StatColumn());
  apps.add(new TideIndexApplication());
  apps.add(new TideSearchApplication());

  string targetApp = Params::GetString("tool name");
  if (targetApp == "list") {
    // List the applications available for create-docs
    for (vector<CruxApplication*>::const_iterator i = apps.begin(); i != apps.end(); i++) {
      cout << (*i)->getName() << endl;
    }
  } else if (targetApp == "default-params") {
    // Write a default parameter file
    cout << "########################################"
            "########################################" << endl
         << "# Sample parameter file" << endl
         << "#" << endl
         << "# On each line, anything after a '#' will be ignored." << endl
         << "# The format is:" << endl
         << "#" << endl
         << "# <parameter-name>=<value>" << endl
         << "#" << endl
         << "########################################"
            "########################################" << endl
         << endl;
    Params::Write(&cout, true);
  } else if (targetApp == "check-params") {
    // Check for issues with parameters
    checkParams(&apps);
  } else {
    CruxApplication* app = apps.find(targetApp);
    if (app == NULL) {
      carp(CARP_FATAL, "Invalid application '%s'", targetApp.c_str());
    }
    generateToolHtml(&cout, app);
  }

  return 0;
}

void CreateDocs::checkParams(const CruxApplicationList* apps) {
  carp(CARP_INFO, "Running parameter validity checks...");
  for (map<string, Param*>::const_iterator i = Params::BeginAll();
       i != Params::EndAll();
       i++) {
    Param* param = i->second;
    string name = param->GetName();
    bool isArg = param->IsArgument();
    // Check for unused if passed an application list
    if (apps) {
      vector<CruxApplication*> appsUsing;
      int appArgCount = 0;
      int appOptionCount = 0;
      for (vector<CruxApplication*>::const_iterator j = apps->begin();
           j != apps->end();
           j++) {
        vector<string> appArgs = (*j)->getArgs();
        for (vector<string>::iterator k = appArgs.begin(); k != appArgs.end(); k++) {
          if (StringUtils::EndsWith(*k, "+")) {
            *k = k->substr(k->length() - 1);
          }
        }
        vector<string> appOptions = (*j)->getOptions();
        bool appArg =
          std::find(appArgs.begin(), appArgs.end(), name) != appArgs.end();
        bool appOption =
          std::find(appOptions.begin(), appOptions.end(), name) != appOptions.end();
        if (!appArg && !appOption) {
          continue;
        }
        string appName = (*j)->getName();
        appsUsing.push_back(*j);
        if (appArg) {
          ++appArgCount;
          if (!isArg) {
            carp(CARP_WARNING, "'%s' is an option, but is listed as an argument for '%s'",
                 name.c_str(), appName.c_str());
          }
        }
        if (appOption) {
          ++appOptionCount;
          if (isArg) {
            carp(CARP_WARNING, "'%s' is an argument, but is listed as an option for '%s'",
                 name.c_str(), appName.c_str());
          }
          if (!Params::IsVisible(name)) {
            carp(CARP_WARNING, "'%s' is marked as hidden, but is listed as an option for '%s'",
                 name.c_str(), appName.c_str());
          }
        }
      }
      if (appArgCount > 0 && appOptionCount > 0) {
        carp(CARP_WARNING, "'%s' is both an option and an argument", name.c_str());
      }
      if (!appsUsing.empty()) {
        stringstream ss;
        for (vector<CruxApplication*>::const_iterator j = appsUsing.begin();
             j != appsUsing.end();
             j++) {
          if (j > appsUsing.begin()) {
            ss << ", ";
          }
          ss << (*j)->getName();
        }
        carp(CARP_DEBUG, "'%s' is used by: %s", name.c_str(), ss.str().c_str());
      } else {
        carp(CARP_WARNING, "No applications are using '%s'", name.c_str());
      }
    }
  }
}

void CreateDocs::generateToolHtml(
  ostream* outStream,
  const CruxApplication* application
) {
  string doc = TOOL_TEMPLATE;
  string inputTemplate = TOOL_INPUT_TEMPLATE;
  string outputTemplate = TOOL_OUTPUT_TEMPLATE;
  string categoryTemplate = TOOL_OPTION_CATEGORY_TEMPLATE;
  string optionTemplate = TOOL_OPTION_TEMPLATE;

  string appName = application->getName();
  string appDescription = application->getDescription();
  vector<string> args = application->getArgs();
  map<string, string> out = application->getOutputs();
  vector<string> opts = application->getOptions();

  // Build usage and input strings
  string usage = "crux " + appName + " [options]";
  string inputs;
  for (vector<string>::const_iterator i = args.begin(); i != args.end(); i++) {
    bool multiArg = StringUtils::EndsWith(*i, "+");
    string argName = multiArg ? i->substr(0, i->length() - 1) : *i;
    usage += " &lt;" + argName + "&gt;";
    if (multiArg) {
      usage += "+";
    }

    if (!Params::Exists(argName)) {
      carp(CARP_FATAL, "Invalid argument '%s' for application '%s'",
           argName.c_str(), appName.c_str());
    }
    string single = inputTemplate;
    map<string, string> replaceMap;
    replaceMap["#NAME#"] = !multiArg ? "&lt;" + argName + "&gt;" : "&lt;" + argName + "&gt;+";
    replaceMap["#DESCRIPTION#"] = Params::ProcessHtmlDocTags(Params::GetUsage(argName), true);
    makeReplacements(&single, replaceMap);
    inputs += single;
  }
  // Build outputs introductory string
  string outputsIntro;
  if (application->needsOutputDirectory()) {
    outputsIntro = "<p>The program writes files to the folder <code>" +
      Params::GetStringDefault("output-dir") + "</code> by default. The name "
      "of the output folder can be set by the user using the <code>--output-dir"
      "</code> option. The following files will be created:\n";
  }
  // Build outputs string
  string outputs;
  for (map<string, string>::const_iterator i = out.begin(); i != out.end(); i++) {
    string single = outputTemplate;
    map<string, string> replaceMap;
    replaceMap["#NAME#"] = i->first;
    replaceMap["#DESCRIPTION#"] = Params::ProcessHtmlDocTags(i->second, true);
    makeReplacements(&single, replaceMap);
    outputs += single;
  }
  // Build options string
  string options;
  for (int i = opts.size() - 1; i >= 0; i--) {
    if (!Params::IsVisible(opts[i])) {
      opts.erase(opts.begin() + i);
    }
  }
  vector< pair< string, vector<string> > > categories = Params::GroupByCategory(opts); 
  for (vector< pair< string, vector<string> > >::const_iterator i = categories.begin();
       i != categories.end();
       i++) {
    string categoryName = i->first;
    if (categoryName.empty()) {
      // Give category to all uncategorized options
      categoryName = appName + " options";
    }
    string optionSubset;
    const vector<string>& items = i->second;
    for (vector<string>::const_iterator j = items.begin(); j != items.end(); j++) {
      if (!Params::Exists(*j)) {
        carp(CARP_FATAL, "Invalid option '%s' for application '%s'",
             j->c_str(), appName.c_str());
      }
      string single = optionTemplate;
      map<string, string> replaceMap;
      replaceMap["#NAME#"] = *j;
      replaceMap["#DESCRIPTION#"] = Params::ProcessHtmlDocTags(Params::GetUsage(*j), true);
      replaceMap["#VALUES#"] = Params::GetAcceptedValues(*j);
      string defaultOutput = Params::GetStringDefault(*j);
      replaceMap["#DEFAULT#"] = !defaultOutput.empty() ? defaultOutput : "&lt;empty&gt;";
      makeReplacements(&single, replaceMap);
      optionSubset += single;
    }

    string single = categoryTemplate;
    map<string, string> replaceMap;
    replaceMap["#NAME#"] = categoryName;
    replaceMap["#OPTIONS#"] = optionSubset;
    makeReplacements(&single, replaceMap);
    options += single;
  }
  if (options.empty()) {
    options = TOOL_NO_OPTIONS_TEMPLATE;
  }

  map<string, string> replacements;
  replacements["#NAME#"] = appName;
  replacements["#DESCRIPTION#"] = Params::ProcessHtmlDocTags(appDescription, true);
  replacements["#USAGE#"] = usage;
  replacements["#INPUTS#"] = inputs;
  replacements["#OUTPUTSINTRODUCTION#"] = outputsIntro;
  replacements["#OUTPUTS#"] = outputs;
  replacements["#OPTIONS#"] = options;
  makeReplacements(&doc, replacements);
  *outStream << doc;
}

void CreateDocs::makeReplacements(
  string* templateStr,
  const map<string, string>& replacements
) {
  const string OPEN_TAG = "<!--";
  const string CLOSE_TAG = "-->";

  unsigned idx, end_idx = 0;
  while ((idx = templateStr->find(OPEN_TAG, end_idx)) != string::npos) {
    if (idx > templateStr->length()) {
      break;
    }
    idx += OPEN_TAG.length();
    end_idx = templateStr->find(CLOSE_TAG, idx);
    if (end_idx == string::npos) {
      break;
    }
    string comment = templateStr->substr(idx, end_idx - idx);
    comment.erase(comment.begin(),
                  find_if(comment.begin(), comment.end(),
                  not1(ptr_fun<int, int>(isspace))));
    comment.erase(find_if(comment.rbegin(), comment.rend(),
                  not1(ptr_fun<int, int>(isspace))).base(), comment.end());
    map<string, string>::const_iterator iter = replacements.find(comment);
    if (iter == replacements.end()) {
      continue;
    }
    idx -= OPEN_TAG.length();
    end_idx += CLOSE_TAG.length();
    templateStr->replace(idx, end_idx - idx, iter->second);
    end_idx = idx + iter->second.length();
  }
}

string CreateDocs::getName() const {
  return "create-docs";
}

string CreateDocs::getDescription() const {
  return "[[html:<p>]]This command prints to standard output an HTML formatted version of the documentation for a specified Crux command.[[html:</p>]]";
}

vector<string> CreateDocs::getArgs() const {
  string arr[] = {
    "tool name"
  };
  return vector<string>(arr, arr + sizeof(arr) / sizeof(string));
}

vector<string> CreateDocs::getOptions() const {
  return vector<string>();
}

map<string, string> CreateDocs::getOutputs() const {
  map<string, string> outputs;
  outputs["stdout"] =
    "The command prints to standard output the HTML documentation for the specified Crux tool.";
  return outputs;
}

bool CreateDocs::needsOutputDirectory() const {
  return false;
}

bool CreateDocs::hidden() const {
  return true;
}

const string CreateDocs::TOOL_TEMPLATE =
  "<!DOCTYPE HTML>\n"
  "<html>\n"
  "<head>\n"
  "<meta charset=\"UTF-8\">\n"
  "<title>crux <!-- #NAME# --></title>\n"
  "<script type=\"text/javascript\"\n"
  "  src=\"http://cdn.mathjax.org/mathjax/latest/MathJax.js?config=TeX-AMS-MML_HTMLorMML\">\n"
  "</script>\n"
  "<script type=\"text/javascript\">\n"
  "  MathJax.Hub.Config({jax: [\"input/TeX\",\"output/HTML-CSS\"], displayAlign: \"left\"});\n"
  "</script>\n"
  "</head>\n"
  "<body>\n"
  "<h1><!-- #NAME# --></h1>\n"
  "<h2>Usage:</h2>\n"
  "<p><code><!-- #USAGE# --></code></p>\n"
  "<h2>Description:</h2>\n"
  "<!-- #DESCRIPTION# -->\n"
  "<h2>Input:</h2>\n"
  "<ul>\n"
  "<!-- #INPUTS# --></ul>\n"
  "<h2>Output:</h2>\n"
  "<!-- #OUTPUTSINTRODUCTION# -->"
  "<ul>\n"
  "<!-- #OUTPUTS# --></ul>\n"
  "<h2>Options:</h2>\n"
  "<ul style=\"list-style-type: none;\">\n"
  "<!-- #OPTIONS# -->\n"
  "</ul>\n"
  "<hr>\n"
  "<a href=\"/\">Home</a>\n"
  "</body>\n"
  "</html>\n";

const string CreateDocs::TOOL_INPUT_TEMPLATE =
  "  <li><code><!-- #NAME# --></code> &ndash; <!-- #DESCRIPTION# --></li>\n";

const string CreateDocs::TOOL_OUTPUT_TEMPLATE =
  "  <li><code><!-- #NAME# --></code> &ndash; <!-- #DESCRIPTION# --></li>\n";

const string CreateDocs::TOOL_OPTION_CATEGORY_TEMPLATE =
  "<li>\n"
  "<h3><!-- #NAME# --></h3>\n"
  "<ul>\n"
  "<!-- #OPTIONS# --></ul>\n"
  "</li>\n";

const string CreateDocs::TOOL_NO_OPTIONS_TEMPLATE =
  "<li>\n"
  "<p>This command does not support any optional parameters.</p>\n"
  "</li>\n";

const string CreateDocs::TOOL_OPTION_TEMPLATE =
  "  <li><code>--<!-- #NAME# --> &lt;<!-- #VALUES# -->&gt;</code> &ndash; "
  "<!-- #DESCRIPTION# --> Default = <code><!-- #DEFAULT# --></code>.</li>\n";

