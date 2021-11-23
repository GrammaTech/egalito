from typing import List, Dict
from collections import defaultdict
import json

JSON_REPORT_NAME='gl-sast-report.json'

link_template = "sast-report-{severity}"
row_template ='<tr><td><a href="#{target}">{severity}</a></td> <td>{count}</td></tr>'
subreport_row_template = """
<tr>
    <td>{location}</td>
    <td>{message}</td>
</tr>
"""

css="""
html {
  font-family: arial; 
}
table {
  border: 2px solid;
  border-collapse: separate;
  border-spacing: 0 3px;
}
td {
    font-size: larger;
}
td:nth-of-type(1){
    min-width: 8em;
    text-align: center;
}
thead {
  font-size: x-large
}
thead tr th {
    border-bottom: 1px solid;
    padding: 1px 5px;
}
tr:nth-of-type(even) {
  background-color:#ccc;
}
"""

report_template = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title> Egalito SAST Report</title>
    <style>{style}</style>
</head>
<body>
    <h1> Egalito SAST Report: {name}</h1>
    <h2> Overview </h2>
    <table id=table-totals>
        <caption hidden=true> Number of SAST issues in Egalito by severity </caption>
        <thead>
            <tr>
                <th scope="col"> Issue Severity</th>
                <th scope="col"> Number of Issues </th>
            </tr>
        </thead>
        <tbody>
            {tbody}
        </tbody>
    </table>
    {subreports}
</body>
</html>
"""

subreport_template = """
    <br />
    <h2 id={link}> {name}-severity Issues </h2>
    <br />
    <table class=dataframe>
        <thead> <tr>
            <th scope="col"> Location </th>
            <th scope="col"> Message </th>
        </tr> </thead>
        <tbody>
            {tbody}
        </tbody>
    </table>
    <br /> 
</body>
"""


def get_issues(report: Dict) -> List[Dict]:
    return report['vulnerabilities']

def get_severity(sast_entry: Dict) -> str:
    return sast_entry['severity']

def get_tool_name(sast_entry: Dict) -> str:
    return sast_entry['scanner']['name']

def get_location(entry: Dict) -> str:
    return "{file}::{start_line}".format(**entry['location'])

def get_message(entry: Dict) -> str:
    return entry['message']

severity_levels = ["Critical", "High", "Medium", "Low", "Info", "Unknown"]

def print_report(data: List[Dict], dest: str = 'sast-report.html'):
    severities = [get_severity(entry) for entry in data]
    name = get_tool_name(data[0])

    counts = [sum(int(s==l) for s in severities) for l in severity_levels ]
    table_body = "\n".join(
        row_template.format(
            target=link_template.format(severity=str(level).lower()),
            severity=level, count=count) for (level,count) in zip(severity_levels,counts)
    )

    subreports = []
    for level in severity_levels:
        subtable = [entry for entry in data if get_severity(entry) == level ]
        if subtable:
            subreports.append(print_subreport(subtable, level))
    
    index = report_template.format(
        name=name,
        tbody=table_body,
        style=css,
        subreports="\n".join(subreports))
    with open(dest, 'w') as sast_report:
        sast_report.write(index)

def print_subreport(subtable: List[Dict], name: str):
    locations = [get_location(x) for x in subtable]
    messages = [get_message(x) for x in subtable]

    tbody = "\n".join(
        subreport_row_template.format(location=l, message=m) 
        for (l,m) in zip(locations, messages)
    )
    subreport = subreport_template.format(
        tbody=tbody,
        name=name,
        link = link_template.format(severity=name.lower()),
    )
    return subreport

if __name__ == "__main__":
    with open(JSON_REPORT_NAME) as jf:
        json_report = json.load(jf, object_hook=(lambda x: defaultdict(lambda : "n/a", x)))
    print_report(get_issues(json_report))
    