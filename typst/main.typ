#set cite(form: "normal", style: "alphanumeric")
#set figure(placement: auto)

#set heading(numbering: "1.")
#set page(
  paper: "us-letter",
  header: align(right)[],
  numbering: "1",
)
#set par(justify: true)
#set text(
  font: "New Computer Modern",
  size: 10pt,
)

#let appendix(body) = {
  set heading(numbering: "A.", supplement: [Appendix])
  counter(heading).update(0)
  body
}

#set table(
  stroke: none,
)

#show table: t => {
  set text(size: 9pt)
  t
}

#align(center, text(17pt)[
  * SPM Project: Distributed External Memory Sorting*
])
#grid(
  columns: (1fr),
  align(center)[
    Marco Pampaloni \
    Department of Computer Science \
    #link("m.pampaloni2@studenti.unipi.it") \
    #datetime.today().display("[month repr:long] [day], [year]")
  ]
)

#outline()
#pagebreak()

/*************************************************************************************/ 
= Introduction <introduction>
== Problem Statement

/*************************************************************************************/ 
= Implementation
== Architecture

== Shared-Memory Algorithm
=== FastFlow
=== OpenMP

== Distributed Algorithm
=== MPI + OpenMP

/*************************************************************************************/ 
= Results
== Performance Evaluation
== Scaling

= Cost Model Analysis
== Bottlenecks
== Main Optimizations
=== Buffered IO
=== Heap Arenas
=== Async IO
=== Computation-Communication Overlap
== Challenges

/*************************************************************************************/ 
= Conclusions
