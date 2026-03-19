# Project Requirements

## Core functional specifications

1. The synthesiser shall play the appropriate musical tone as a sawtooth wave when a key is pressed
2. There shall be no perceptible delay between pressing a key and the tone starting
3. There shall be a volume control with at least 8 increments, which shall be operated by turning a knob
4. The OLED display shall show the name of the note being played and the current volume level
5. Every 100ms the OLED display shall refresh and an LED shall toggle
6. The synthesiser shall be configurable, during compilation or operation, to act as a sender module or receiver module.
7. If the synthesiser is configured as a sender, it shall send a message on the CAN bus whenever a key is pressed or released
8. If the synthesiser is configured as a receiver, it shall play a note or stop playing a note when it receives an appropriate message on the CAN bus

## Non-functional specifications

9. The system shall be implemented using interrupts and threads to achieve concurrent execution of tasks
10. All data and other resources that are accessed by multiple tasks shall be protected against errors caused by simultaneous access
11. The code shall be well-structured and maintainable
12. The code shall contain compile-time options for measuring the execution time of each task

## Documentation specifications

13. The report shall be presented as documentation in the GitHub repository for your code, consisting of one or more markdown files linked to a table of contents
14. The report shall contain:
    * An identification of all the tasks that are performed by the system with their method of implementation: thread or interrupt
15. A characterisation of each task with its theoretical minimum initiation interval (including assumptions used) and measured maximum execution time
16. A critical instant analysis of the rate monotonic scheduler, showing that all deadlines are met under worst-case conditions
17. A quantification of total CPU utilisation
18. An identification of all the shared data structures and the methods used to guarantee safe access and synchronisation
19. An analysis of inter-task blocking dependencies that shows any possibility of deadlock