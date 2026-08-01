using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class BushShake : MonoBehaviour

{    
    private BoxCollider2D boxCollider;
    private GameObject bush;
    private Collider2D trCollider;
    
    void Start()
    {
        boxCollider = GetComponent<BoxCollider2D>();
        bush = GetComponent<GameObject>();
    }
       

    private void OnTriggerEnter2D(Collider2D other)
    {
        trCollider = other;

        if (trCollider.gameObject.tag == "Player")
        {
            gameObject.transform.Rotate(0, 25, 0);
            StartCoroutine(Coroutine());
            
        }
    }

    private IEnumerator Coroutine()
    {
        yield return new WaitForSeconds(0.07f);
        gameObject.transform.Rotate(0, -25, 0);

        yield return new WaitForSeconds(0.1f);
        gameObject.transform.Rotate(0, 20, 0);

        yield return new WaitForSeconds(0.07f);
        gameObject.transform.Rotate(0, -20, 0);

    }    
   
}
