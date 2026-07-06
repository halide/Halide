use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

pub fn to_dot(dag_edges: &BTreeMap<String, BTreeSet<String>>) -> String {
    let mut dot = String::from("strict digraph {\n\trankdir = LR\n\n");

    for (key, value) in dag_edges.iter() {
        for dest in value {
            write!(dot, "\t{key} -> {dest}\n").unwrap_or_default();
        }
    }

    write!(dot, "}}").unwrap_or_default();

    dot
}
