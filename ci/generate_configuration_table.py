#!/usr/bin/python3

"""
Generate a table of the Graphviz compile configuration for different platforms
from files generated by the ./configure command.
"""

import os
import re
import sys
from argparse import ArgumentParser


def main():
    """entry point"""

    parser = ArgumentParser(
        description="Generate a table of the Graphviz compile "
        "configuration for different platforms from files "
        "generated by the ./configure command."
    )
    parser.add_argument(
        "--color",
        "-c",
        action="store_const",
        dest="colors",
        const="green:red",
        help="Color output using default coloring. Yes is "
        "colored green and No is colored red",
    )
    parser.add_argument(
        "--colors",
        help="Color output using specified COLORS. The format is "
        "<Yes-color>:<No-color>",
    )
    parser.add_argument(
        "--short", "-s", action="store_true", help="Output only Yes or No"
    )
    parser.add_argument("filename", nargs="*", help="Configuration log to read")

    opts = parser.parse_args()

    if opts.colors is None:
        styles = {
            "Yes": "",
            "No": "",
        }
    else:
        yes_color = "green"
        no_color = "red"
        if opts.colors != "":
            colors = opts.colors.split(":")
            if len(colors) == 2:
                yes_color, no_color = colors
            else:
                sys.stderr.write(
                    f"Error: {opts.colors} color specification is illegal\n"
                )
                parser.print_help(file=sys.stderr)
                sys.exit(1)
        styles = {
            "Yes": f' style="color: {yes_color};"',
            "No": f' style="color: {no_color};"',
        }

    table = {}
    table_sections = []
    component_names = {}
    platforms = []

    for filename in opts.filename:
        os_path = os.path.dirname(filename)
        os_version_id = os.path.basename(os_path)
        os_id = os.path.basename(os.path.dirname(os_path))
        platform = f"{os_id.capitalize()} {os_version_id}"
        if platform not in platforms:
            platforms.append(platform)
        with open(filename, "rt", encoding="utf-8") as fp:
            for line in fp.readlines():
                item = [item.strip() for item in line.split(":")]
                if len(item) == 2:
                    if item[1] == "":
                        section_name = item[0]
                        if section_name not in table:
                            table[section_name] = {}
                            table_sections.append(section_name)
                            component_names[section_name] = []
                    else:
                        component_name, component_value = item
                        short_value = re.sub("(Yes|No).*", "\\1", component_value)
                        if opts.short:
                            component_value = short_value
                        if component_name not in table[section_name]:
                            table[section_name][component_name] = {}
                            component_names[section_name].append(component_name)
                        table[section_name][component_name][platform] = component_value

    colspan = len(platforms) + 1
    indent = ""
    print(f"{indent}<!DOCTYPE html>")
    print(f"{indent}<html>")
    indent += "  "
    print(f"{indent}<head>")
    indent += "  "
    print(f'{indent}<meta charset="utf-8">')
    print(f"{indent}<style>")
    indent += "  "
    print(
        f"{indent}table {{text-align: left; white-space: nowrap; position: relative;}}"
    )
    print(
        f"{indent}thead tr th {{padding-right: 1em; padding-bottom: 5px; "
        "position: sticky; top: 0px;  background-color: white;}"
    )
    print(f"{indent}td, th {{padding-left: 1.5em;}}")
    indent = indent[:-2]
    print(f"{indent}</style>")
    indent = indent[:-2]
    print(f"{indent}</head>")
    print(f"{indent}<body>")
    indent += "  "
    header = table_sections[0].replace("will be", "was")
    print(f"{indent}<h1>{header}:</h1>")
    print(f"{indent}<table>")
    indent += "  "
    print(f"{indent}<thead>")
    indent += "  "
    print(f"{indent}<tr>")
    indent += "  "
    print(f"{indent}<th></th>")
    for platform in platforms:
        print(f"{indent}<th>{platform}</th>")
    indent = indent[:-2]
    print(f"{indent} </tr>")
    print(f"{indent} </thead>")
    print(f"{indent} <tbody>")
    indent += "  "
    indent = indent[:-2]
    for section_name in table_sections[1:]:
        print(f'{indent}<tr><th colspan="{colspan}">{section_name};</th></tr>')
        for component_name in component_names[section_name]:
            print(f"{indent}<tr>")
            indent += "  "
            print(f"{indent}<td>{component_name}</td>")
            for platform in platforms:
                component_value = table[section_name][component_name][platform]
                short_value = re.sub("(Yes|No).*", "\\1", component_value)
                color_style = styles[short_value]
                print(f"{indent}<td{color_style}>{component_value}</td>")
            indent = indent[:-2]
            print(f"{indent}</tr>")
        print(f'{indent}<tr><th colspan="{colspan}">&nbsp;</th></tr>')
    indent = indent[:-2]
    print(f"{indent}</tbody>")
    indent = indent[:-2]
    print(f"{indent}</table>")
    indent = indent[:-2]
    print(f"{indent}</body>")
    indent = indent[:-2]
    print(f"{indent}</html>")


# Python trick to get a main routine
if __name__ == "__main__":
    main()
